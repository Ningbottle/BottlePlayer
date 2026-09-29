/**
 * Web Audio API EQ controller for the HTML5 <audio> backend.
 *
 * Phase 2 of the AudioWorklet redesign: routes audio via captureStream →
 * MediaStreamAudioSourceNode → AudioWorkletNode (10-band RBJ peaking EQ) →
 * GainNode → destination. The <audio> element is never passed through
 * createMediaElementSource.
 *
 * See docs/superpowers/specs/2026-06-28-eq-audioworklet-redesign-design.md §3.1, §4.2.
 */

import { clampEqGain } from './equalizerConfig';
import { loadEqWorklet, type AudioContextForWorklet } from './eqWorkletProcessor';

export interface EqOptions {
  enabled: boolean;
  bands: number[];
  /** When false, skip captureStream attach (element plays directly). */
  crossOriginSafe?: boolean;
  /** Called when AudioContext cannot resume or worklet load fails. */
  onDegraded?: () => void;
  /** Called when resume succeeds after a prior degradation. */
  onRecovered?: () => void;
}

export interface AudioNodeLike {
  connect(dest: AudioNodeLike): AudioNodeLike;
  disconnect(dest?: AudioNodeLike): void;
}

export interface AudioParamLike {
  value: number;
  cancelScheduledValues(time: number): AudioParamLike;
  setValueAtTime(value: number, time: number): AudioParamLike;
  linearRampToValueAtTime(value: number, time: number): AudioParamLike;
}

export interface GainNodeLike extends AudioNodeLike {
  gain: AudioParamLike;
}

export interface AudioWorkletNodeLike extends AudioNodeLike {
  port: { postMessage(data: unknown): void };
}

export interface AudioContextLike extends AudioContextForWorklet {
  state: string;
  currentTime: number;
  destination: AudioNodeLike;
  resume(): Promise<void>;
  close(): Promise<void>;
  createGain(): GainNodeLike;
  createMediaStreamSource(stream: MediaStream): AudioNodeLike;
}

export type AudioContextFactory = () => AudioContextLike | null;

export type LoadEqWorkletFn = typeof loadEqWorklet;

export interface WebAudioEqDeps {
  loadWorklet?: LoadEqWorkletFn;
  WorkletNodeCtor?: new (ctx: unknown, name: string) => AudioWorkletNodeLike;
}

/** HTMLAudioElement with captureStream (Chrome / WebView2). */
interface CapturableAudioElement extends HTMLAudioElement {
  captureStream(): MediaStream;
}

const GAIN_CROSSFADE_MS = 50;

export class WebAudioEq {
  private ctx: AudioContextLike | null = null;
  private workletNode: AudioWorkletNodeLike | null = null;
  private gainNode: GainNodeLike | null = null;
  private sourceNode: AudioNodeLike | null = null;
  private currentStream: MediaStream | null = null;
  private rerouted = false;
  private bands: number[] = [];
  private enabled = false;
  private initStarted = false;
  private workletFailed = false;
  private blobUrl: string | null = null;
  private readyPromise: Promise<void> | null = null;
  private onDegradedCb?: () => void;
  private onRecoveredCb?: () => void;
  /** Last user volume on the gainNode path (preserved across degradation). */
  private outputVolume = 1;
  private degradationTimer: ReturnType<typeof setTimeout> | null = null;
  private currentAudio: HTMLAudioElement | null = null;
  private fallbackElementVolume = 1;
  private leaseId = 0;
  /** Invalidates in-flight graph construction when close/re-init changes ownership. */
  private graphGeneration = 0;

  constructor(
    private readonly createCtx: AudioContextFactory,
    private readonly deps: WebAudioEqDeps = {},
  ) {}

  /** Build the long-lived worklet graph on first use and update its live options. */
  init(opts: EqOptions): void {
    this.doInit(opts);
  }

  attachSource(audio: HTMLAudioElement, fallbackVolume: number): boolean {
    if (!this.ctx || !this.workletNode || this.workletFailed) return false;

    this.disconnectSource();

    let stream: MediaStream | null = null;
    let source: AudioNodeLike | null = null;
    try {
      stream = (audio as CapturableAudioElement).captureStream();
      source = this.ctx.createMediaStreamSource(stream);
      source.connect(this.workletNode);
    } catch {
      try {
        source?.disconnect();
      } catch {
        /* already disconnected or never connected */
      }
      stream?.getAudioTracks().forEach((t) => t.stop());
      audio.volume = fallbackVolume;
      this.currentAudio = null;
      this.sourceNode = null;
      this.currentStream = null;
      this.rerouted = false;
      this.onDegradedCb?.();
      return false;
    }

    this.currentAudio = audio;
    this.fallbackElementVolume = fallbackVolume;
    this.currentStream = stream;
    this.sourceNode = source;
    this.leaseId += 1;
    audio.volume = 0;
    this.rerouted = true;
    return true;
  }

  get currentLeaseId(): number {
    return this.leaseId;
  }

  get contextState(): string {
    return this.ctx?.state ?? 'closed';
  }

  get isGraphReady(): boolean {
    return !!this.ctx && !!this.workletNode && !!this.gainNode && !this.workletFailed;
  }

  releaseLease(leaseId: number): void {
    if (leaseId !== this.leaseId) return;
    this.disconnectSource();
  }

  disconnectSource(): void {
    this.clearDegradationTimer();
    const ownedAudio = this.currentAudio;
    const restoreVolume = this.fallbackElementVolume;
    if (this.sourceNode) {
      this.sourceNode.disconnect();
      this.sourceNode = null;
    }
    if (this.currentStream) {
      this.currentStream.getAudioTracks().forEach((t) => t.stop());
      this.currentStream = null;
    }
    this.rerouted = false;
    this.currentAudio = null;
    if (ownedAudio) {
      ownedAudio.volume = restoreVolume;
    }
  }

  setBand(index: number, gainDb: number, enabled: boolean): void {
    if (!this.workletNode || this.workletFailed || index < 0 || index >= this.bands.length) {
      return;
    }
    if (!enabled) return;
    this.bands[index] = clampEqGain(gainDb);
    this.workletNode.port.postMessage({ type: 'setBands', bands: [...this.bands] });
  }

  setEnabled(enabled: boolean, bands: number[]): void {
    if (!this.workletNode || this.workletFailed) return;
    this.enabled = enabled;
    this.bands = bands.map((g) => (enabled ? clampEqGain(g ?? 0) : 0));
    this.workletNode.port.postMessage({ type: 'setEnabled', enabled });
    if (enabled) {
      this.workletNode.port.postMessage({ type: 'setBands', bands: [...this.bands] });
    }
  }

  /** User volume when EQ is rerouted (gainNode path). No-op when not rerouted. */
  setVolume(vol: number): void {
    this.outputVolume = vol;
    if (!this.rerouted) return;
    this.fallbackElementVolume = vol;
    if (!this.gainNode) return;
    this.gainNode.gain.value = vol;
  }

  get isRerouted(): boolean {
    return this.rerouted;
  }

  /** Resolves when the worklet graph has finished initializing (Phase 2 transitional). */
  awaitReady(): Promise<void> {
    return this.whenReady();
  }

  async resume(): Promise<void> {
    if (!this.ctx || this.workletFailed) return;
    if (this.ctx.state === 'suspended') {
      await this.ctx.resume();
    }
  }

  close(): void {
    this.graphGeneration += 1;
    this.clearDegradationTimer();
    this.disconnectSource();
    const ctx = this.ctx;
    const workletNode = this.workletNode;
    const gainNode = this.gainNode;
    const blobUrl = this.blobUrl;
    this.ctx = null;
    this.workletNode = null;
    this.gainNode = null;
    this.blobUrl = null;
    this.initStarted = false;
    this.workletFailed = false;
    this.readyPromise = null;
    this.releaseGraphResources(ctx, workletNode, gainNode, blobUrl);
  }

  /** §3.3 / §4.4: fade gainNode out, then release the volume lease. */
  enterDegradation(audio: HTMLAudioElement, vol: number): void {
    this.clearDegradationTimer();
    this.currentAudio = audio;
    this.fallbackElementVolume = vol;
    this.rampGainTo(0);
    this.degradationTimer = setTimeout(() => {
      this.degradationTimer = null;
      this.disconnectSource();
      this.onDegradedCb?.();
    }, GAIN_CROSSFADE_MS);
  }

  /** §3.3 / §4.4: reattach via the atomic lease, then fade gainNode back in. */
  recoverFromDegradation(audio: HTMLAudioElement, fallbackVolume: number): boolean {
    this.clearDegradationTimer();
    const ok = this.attachSource(audio, fallbackVolume);
    if (!ok) return false;
    this.rampGainTo(this.outputVolume);
    this.onRecoveredCb?.();
    return true;
  }

  private doInit(opts: EqOptions): void {
    this.onDegradedCb = this.resolveOnDegraded(opts);
    this.onRecoveredCb = opts.onRecovered;
    this.enabled = opts.enabled;
    this.bands = opts.bands.map((g) => (opts.enabled ? clampEqGain(g ?? 0) : 0));

    if (this.initStarted) {
      if (this.workletNode && !this.workletFailed) {
        this.postBands();
        this.postEnabled(opts.enabled);
      }
      return;
    }

    this.initStarted = true;
    this.workletFailed = false;
    const generation = ++this.graphGeneration;
    this.readyPromise = this.buildGraph(generation);
  }

  private async buildGraph(generation: number): Promise<void> {
    let ctx: AudioContextLike | null = null;
    let workletNode: AudioWorkletNodeLike | null = null;
    let gainNode: GainNodeLike | null = null;
    let blobUrl: string | null = null;
    try {
      ctx = this.createCtx();
      if (!ctx) throw new Error('AudioContext is unavailable');

      const loadWorklet = this.deps.loadWorklet ?? loadEqWorklet;
      const globalWorkletCtor = (
        globalThis as typeof globalThis & {
          AudioWorkletNode?: new (context: unknown, name: string) => AudioWorkletNodeLike;
        }
      ).AudioWorkletNode;
      const WorkletNodeCtor = this.deps.WorkletNodeCtor ?? globalWorkletCtor;
      if (!WorkletNodeCtor) throw new Error('AudioWorkletNode is unavailable');

      blobUrl = await loadWorklet(ctx);
      if (generation !== this.graphGeneration) {
        this.releaseGraphResources(ctx, null, null, blobUrl);
        return;
      }

      workletNode = new WorkletNodeCtor(ctx, 'eq-processor');
      gainNode = ctx.createGain();
      gainNode.gain.value = 1;
      workletNode.connect(gainNode);
      gainNode.connect(ctx.destination);
      workletNode.port.postMessage({ type: 'setBands', bands: [...this.bands] });
      workletNode.port.postMessage({ type: 'setEnabled', enabled: this.enabled });

      if (generation !== this.graphGeneration) {
        this.releaseGraphResources(ctx, workletNode, gainNode, blobUrl);
        return;
      }

      this.ctx = ctx;
      this.workletNode = workletNode;
      this.gainNode = gainNode;
      this.blobUrl = blobUrl;
      this.workletFailed = false;
    } catch (e) {
      this.releaseGraphResources(ctx, workletNode, gainNode, blobUrl);
      if (generation === this.graphGeneration) {
        console.warn('Web Audio API EQ worklet init failed:', e);
        this.initStarted = false;
        this.workletFailed = true;
        this.onDegradedCb?.();
      }
    }
  }

  private releaseGraphResources(
    ctx: AudioContextLike | null,
    workletNode: AudioWorkletNodeLike | null,
    gainNode: GainNodeLike | null,
    blobUrl: string | null,
  ): void {
    try {
      workletNode?.disconnect();
    } catch {
      /* already disconnected or only partially constructed */
    }
    try {
      gainNode?.disconnect();
    } catch {
      /* already disconnected or only partially constructed */
    }
    if (blobUrl) {
      try {
        URL.revokeObjectURL(blobUrl);
      } catch {
        /* URL implementation may already be gone during teardown */
      }
    }
    if (ctx) {
      try {
        ctx.close().catch(() => {});
      } catch {
        /* a partially constructed context can reject synchronous close */
      }
    }
  }

  private postBands(): void {
    this.workletNode?.port.postMessage({ type: 'setBands', bands: [...this.bands] });
  }

  private postEnabled(enabled: boolean): void {
    this.workletNode?.port.postMessage({ type: 'setEnabled', enabled });
  }

  private resolveOnDegraded(opts: EqOptions): (() => void) | undefined {
    return opts.onDegraded;
  }

  private whenReady(): Promise<void> {
    return this.readyPromise ?? Promise.resolve();
  }

  private clearDegradationTimer(): void {
    if (this.degradationTimer !== null) {
      clearTimeout(this.degradationTimer);
      this.degradationTimer = null;
    }
  }

  /** ~50ms linear ramp on gainNode.gain (spec §4.4). */
  private rampGainTo(target: number): void {
    if (!this.ctx || !this.gainNode) return;
    const param = this.gainNode.gain;
    const now = this.ctx.currentTime;
    param.cancelScheduledValues(now);
    param.setValueAtTime(param.value, now);
    param.linearRampToValueAtTime(target, now + GAIN_CROSSFADE_MS / 1000);
  }
}

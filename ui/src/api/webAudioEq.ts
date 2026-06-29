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

export interface GainNodeLike extends AudioNodeLike {
  gain: { value: number };
}

export interface AudioWorkletNodeLike extends AudioNodeLike {
  port: { postMessage(data: unknown): void };
}

export interface AudioContextLike extends AudioContextForWorklet {
  state: string;
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

  constructor(
    private readonly createCtx: AudioContextFactory,
    private readonly deps: WebAudioEqDeps = {},
  ) {}

  /** Build the long-lived worklet graph once at app startup. */
  init(opts: EqOptions): void {
    this.doInit(opts);
  }

  attachSource(audio: HTMLAudioElement): void {
    if (!this.ctx || !this.workletNode || this.workletFailed) return;

    this.disconnectSource();

    const capturable = audio as CapturableAudioElement;
    capturable.volume = 0;
    this.currentStream = capturable.captureStream();
    this.sourceNode = this.ctx.createMediaStreamSource(this.currentStream);
    this.sourceNode.connect(this.workletNode);
    this.rerouted = true;
  }

  disconnectSource(): void {
    if (this.sourceNode) {
      this.sourceNode.disconnect();
      this.sourceNode = null;
    }
    if (this.currentStream) {
      this.currentStream.getAudioTracks().forEach((t) => t.stop());
      this.currentStream = null;
    }
    this.rerouted = false;
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
    if (!this.rerouted || !this.gainNode) return;
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
    this.disconnectSource();
    this.workletNode?.disconnect();
    this.gainNode?.disconnect();
    if (this.blobUrl) {
      URL.revokeObjectURL(this.blobUrl);
      this.blobUrl = null;
    }
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
    this.workletNode = null;
    this.gainNode = null;
    this.initStarted = false;
    this.workletFailed = false;
    this.readyPromise = null;
  }

  /** §3.3: disconnect worklet input before unmute element (anti double-audio). */
  enterDegradation(audio: HTMLAudioElement, vol: number): void {
    if (this.sourceNode && this.workletNode) {
      this.sourceNode.disconnect(this.workletNode);
    }
    audio.volume = vol;
    this.rerouted = false;
    this.onDegradedCb?.();
  }

  /** §3.3: mute element before attachSource (anti double-audio). */
  recoverFromDegradation(audio: HTMLAudioElement): void {
    audio.volume = 0;
    this.attachSource(audio);
    this.onRecoveredCb?.();
  }

  private doInit(opts: EqOptions): void {
    this.onDegradedCb = this.resolveOnDegraded(opts);
    this.onRecoveredCb = opts.onRecovered;

    if (this.initStarted) {
      if (this.workletNode && !this.workletFailed) {
        this.enabled = opts.enabled;
        this.bands = opts.bands.map((g) => (opts.enabled ? clampEqGain(g ?? 0) : 0));
        this.postBands();
        this.postEnabled(opts.enabled);
      }
      return;
    }

    this.initStarted = true;
    this.enabled = opts.enabled;
    this.bands = opts.bands.map((g) => (opts.enabled ? clampEqGain(g ?? 0) : 0));
    this.readyPromise = this.buildGraph(opts);
  }

  private async buildGraph(_opts: EqOptions): Promise<void> {
    const ctx = this.createCtx();
    if (!ctx) {
      this.workletFailed = true;
      this.onDegradedCb?.();
      return;
    }
    this.ctx = ctx;

    const loadWorklet = this.deps.loadWorklet ?? loadEqWorklet;
    const WorkletNodeCtor = this.deps.WorkletNodeCtor
      ?? (AudioWorkletNode as unknown as new (ctx: unknown, name: string) => AudioWorkletNodeLike);

    try {
      this.blobUrl = await loadWorklet(ctx);
      this.workletNode = new WorkletNodeCtor(ctx, 'eq-processor');
      this.gainNode = ctx.createGain();
      this.gainNode.gain.value = 1;
      this.workletNode.connect(this.gainNode);
      this.gainNode.connect(ctx.destination);
      this.postBands();
      this.postEnabled(this.enabled);
    } catch (e) {
      console.warn('Web Audio API EQ worklet init failed:', e);
      this.workletFailed = true;
      this.onDegradedCb?.();
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
}

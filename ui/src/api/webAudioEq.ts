/**
 * Web Audio API EQ controller for the HTML5 <audio> backend.
 *
 * Routes the <audio> element through a 10-band BiquadFilter peaking chain
 * (31/62/125/250/500/1K/2K/4K/8K/16K Hz).
 *
 * Design notes (fixing the EQ bugs from the player-fix design):
 *
 * - #4 Graph build order: the full filter→gain→destination chain is wired
 *   BEFORE createMediaElementSource is called. createMediaElementSource
 *   irreversibly reroutes the element's output into the graph; if it ran first
 *   and a later step threw, the element would be stranded in a disconnected
 *   graph → permanent silence. Building the destination-connected graph first
 *   means any throw happens before the reroute.
 *
 * - #1 CORS: createMediaElementSource requires the media to be same-origin or
 *   CORS-enabled, otherwise it taints (silent PCM) — and setting
 *   crossOrigin='anonymous' on a non-CORS CDN makes the load fail entirely.
 *   KuGou's CDN sends no Access-Control-Allow-Origin (verified 2026-06-25), so
 *   for cross-origin non-CORS sources we skip the graph entirely and the
 *   <audio> plays directly (EQ off for that source). Playback is never broken.
 *
 * - #9 The AudioContext is close()'d on teardown so HMR doesn't leak contexts.
 *
 * - #10 A failed resume() (suspended context, no user gesture) is surfaced via
 *   onSuspendedFail instead of swallowed, so the caller can degrade.
 */

import { EQ_BANDS, clampEqGain } from './equalizerConfig';

export interface EqOptions {
  enabled: boolean;
  bands: number[];
  /** Whether the media source is same-origin or CORS-enabled. When false, the
   *  EQ graph is not built (audio plays directly) to avoid breaking playback. */
  crossOriginSafe?: boolean;
  /** Called when the AudioContext cannot resume (suspended, no gesture). */
  onSuspendedFail?: () => void;
  /** Called when createMediaElementSource throws InvalidStateError — the
   *  <audio> element is already (irreversibly) bound to a previous
   *  MediaElementSourceNode, so EQ can never work on THIS element again.
   *  The caller must swap in a fresh <audio> element to recover playback. */
  onElementWedged?: () => void;
}

export interface AudioContextLike {
  state: string;
  destination: AudioNodeLike;
  resume(): Promise<void>;
  close(): Promise<void>;
  createMediaElementSource(el: HTMLMediaElement): AudioNodeLike;
  createBiquadFilter(): BiquadFilterLike;
  createGain(): GainNodeLike;
}

export interface AudioNodeLike {
  connect(dest: AudioNodeLike): AudioNodeLike;
}
export interface BiquadFilterLike extends AudioNodeLike {
  type: string;
  frequency: { value: number };
  Q: { value: number };
  gain: { value: number };
}
export interface GainNodeLike extends AudioNodeLike {
  gain: { value: number };
}

export type AudioContextFactory = () => AudioContextLike | null;

export class WebAudioEq {
  private ctx: AudioContextLike | null = null;
  private filters: BiquadFilterLike[] = [];
  private source: AudioNodeLike | null = null;
  private rerouted = false;
  private onSuspendedFail?: () => void;
  private onElementWedged?: () => void;

  constructor(private readonly createCtx: AudioContextFactory) {}

  init(audio: HTMLAudioElement, opts: EqOptions): void {
    // If already initialized for this element, do nothing. This guard is ALSO
    // what prevents a second createMediaElementSource call on the same element
    // (which would throw InvalidStateError — see the catch block below).
    if (this.ctx) return;

    const crossOriginSafe = opts.crossOriginSafe !== false;
    // Non-CORS cross-origin media: skip the graph so playback isn't broken.
    if (!crossOriginSafe) {
      return;
    }

    const ctx = this.createCtx();
    if (!ctx) return;
    this.ctx = ctx;
    this.onSuspendedFail = opts.onSuspendedFail;
    this.onElementWedged = opts.onElementWedged;

    try {
      // #4: build the full destination-connected graph FIRST.
      this.filters = EQ_BANDS.map((band, i) => {
        const filter = ctx.createBiquadFilter();
        filter.type = 'peaking';
        filter.frequency.value = band.frequency;
        filter.Q.value = 1 / Math.SQRT2;
        filter.gain.value = opts.enabled ? clampEqGain(opts.bands[i] ?? 0) : 0;
        return filter;
      });

      const gain = ctx.createGain();
      gain.gain.value = 1.0;

      // chain: filter[0] -> filter[1] -> ... -> last filter -> gain -> destination
      for (let i = 0; i < this.filters.length - 1; i++) {
        this.filters[i].connect(this.filters[i + 1]);
      }
      this.filters[this.filters.length - 1].connect(gain);
      gain.connect(ctx.destination);

      // #4: only NOW reroute the element into the (already-connected) graph.
      this.source = ctx.createMediaElementSource(audio);
      this.source.connect(this.filters[0]);
      this.rerouted = true;
    } catch (e) {
      // Best-effort cleanup; the element was only rerouted if source was set.
      this.disposeGraph();
      // #16: InvalidStateError means the <audio> element was already bound to
      // a previous MediaElementSourceNode (from an earlier track). The binding
      // is irreversible for the element's lifetime, so EQ can never work on
      // THIS element again. Signal the caller to swap in a fresh element.
      // Keep this.ctx set so a subsequent init() on the SAME element short-
      // circuits via the guard above (avoids re-throwing on every track).
      if (isElementWedgedError(e)) {
        this.onElementWedged?.();
      } else {
        console.warn('Web Audio API EQ init failed:', e);
      }
    }
  }

  setBand(index: number, gainDb: number, enabled: boolean): void {
    if (!this.ctx || index < 0 || index >= this.filters.length) return;
    if (!enabled) return;
    if (this.filters[index]) {
      this.filters[index].gain.value = clampEqGain(gainDb);
    }
  }

  setEnabled(enabled: boolean, bands: number[]): void {
    if (!this.ctx) return;
    this.filters.forEach((filter, i) => {
      filter.gain.value = enabled ? clampEqGain(bands[i] ?? 0) : 0;
    });
  }

  /** Resume the AudioContext after a user gesture (autoplay policy). */
  async resume(): Promise<void> {
    if (!this.ctx) return;
    if (this.ctx.state === 'suspended') {
      try {
        await this.ctx.resume();
      } catch (e) {
        console.warn('Web Audio API EQ resume failed; EQ will be degraded.', e);
        // #10: surface the failure so the UI can degrade. Do NOT null
        // this.ctx or close the context here — nulling would defeat init()'s
        // `if (this.ctx) return` guard and cause the next init() to call
        // createMediaElementSource again on the already-bound element,
        // throwing InvalidStateError and wedging playback (#16). The graph
        // stays routed; if the context stays suspended the audio is silent,
        // but that is a separate (rarer) failure mode than the element-wedge.
        this.onSuspendedFail?.();
      }
    }
  }

  /** Tear down the context (call on player re-init / HMR zombie cleanup). */
  close(): void {
    this.disposeGraph();
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
  }

  get isRerouted(): boolean {
    return this.rerouted;
  }

  private disposeGraph(): void {
    this.filters = [];
    this.source = null;
    this.rerouted = false;
  }
}

/** Detect the InvalidStateError thrown when createMediaElementSource is called
 *  on an <audio> element that's already bound to a previous source node. The
 *  binding is irreversible for the element's lifetime, so the caller must swap
 *  in a fresh element. */
function isElementWedgedError(e: unknown): boolean {
  const err = e as { name?: string; message?: string };
  if (!err) return false;
  if (err.name === 'InvalidStateError') return true;
  const msg = err.message || '';
  return msg.includes('already connected') || msg.includes('MediaElementSourceNode');
}

/**
 * Web Audio API EQ controller for the HTML5 <audio> backend.
 *
 * Routes the <audio> element through a 5-band BiquadFilter peaking chain
 * (frequencies matching the C++ MFT equalizer: 60/230/910/3600/14000 Hz).
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

const EQ_FREQS = [60, 230, 910, 3600, 14000];

export interface EqOptions {
  enabled: boolean;
  bands: number[];
  /** Whether the media source is same-origin or CORS-enabled. When false, the
   *  EQ graph is not built (audio plays directly) to avoid breaking playback. */
  crossOriginSafe?: boolean;
  /** Called when the AudioContext cannot resume (suspended, no gesture). */
  onSuspendedFail?: () => void;
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

  constructor(private readonly createCtx: AudioContextFactory) {}

  init(audio: HTMLAudioElement, opts: EqOptions): void {
    // If already initialized for this element, do nothing.
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

    try {
      // #4: build the full destination-connected graph FIRST.
      this.filters = EQ_FREQS.map((freq, i) => {
        const filter = ctx.createBiquadFilter();
        filter.type = 'peaking';
        filter.frequency.value = freq;
        filter.Q.value = 1 / Math.SQRT2;
        filter.gain.value = opts.enabled ? opts.bands[i] || 0 : 0;
        return filter;
      });

      const gain = ctx.createGain();
      gain.gain.value = 1.0;

      // chain: filter[0] -> filter[1] -> ... -> filter[4] -> gain -> destination
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
      console.warn('Web Audio API EQ init failed:', e);
      // Best-effort cleanup; the element was only rerouted if source was set.
      this.disposeGraph();
    }
  }

  setBand(index: number, gainDb: number, enabled: boolean): void {
    if (!this.ctx || index < 0 || index >= this.filters.length) return;
    if (!enabled) return;
    if (this.filters[index]) {
      this.filters[index].gain.value = gainDb;
    }
  }

  setEnabled(enabled: boolean, bands: number[]): void {
    if (!this.ctx) return;
    this.filters.forEach((filter, i) => {
      filter.gain.value = enabled ? bands[i] || 0 : 0;
    });
  }

  /** Resume the AudioContext after a user gesture (autoplay policy). */
  async resume(): Promise<void> {
    if (!this.ctx) return;
    if (this.ctx.state === 'suspended') {
      try {
        await this.ctx.resume();
      } catch {
        // #10: surface the failure instead of swallowing it.
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

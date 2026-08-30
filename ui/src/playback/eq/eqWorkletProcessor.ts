/**
 * Phase 1 of the EQ AudioWorklet redesign (see
 * docs/superpowers/specs/2026-06-28-eq-audioworklet-redesign-design.md §4.1).
 *
 * This module is pure DSP + a Blob-URL loader. It must NOT touch the live
 * audio element, the player store, or the existing `webAudioEq.ts` — those
 * are Phase 2/3.
 *
 * Spec deviation (approved in the implementation plan, §"spec 偏差修正"):
 * spec §4.1 says the biquad math is "翻自 native/playback/BiquadFilter.cpp",
 * but that file does NOT exist in the repo (verified under `ui/` and
 * `ui/src-tauri/`). Instead we implement the standard RBJ peaking filter
 * from the Audio EQ Cookbook (Robert Bristow-Johnson), with
 * Q = 1 / Math.SQRT2 to match the existing `BiquadFilterNode` usage.
 */

import { EQ_BANDS } from './equalizerConfig';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** Normalized biquad coefficients (a0 divided through, so a0 === 1 and omitted). */
export interface BiquadCoeffs {
  b0: number;
  b1: number;
  b2: number;
  a1: number;
  a2: number;
}

/** Per-stage, per-channel Direct Form I transposed state. */
export interface BiquadState {
  x1: number;
  x2: number;
  y1: number;
  y2: number;
}

// ---------------------------------------------------------------------------
// Pure DSP functions (unit-testable outside any AudioContext)
// ---------------------------------------------------------------------------

/** Minimum allowed center frequency (Hz). Frequencies below this are clamped. */
export const EQ_MIN_FREQ_HZ = 20;
/**
 * Fraction of Nyquist above which we clamp. Frequencies above `0.95 * sr/2`
 * are clamped — RBJ formulas get numerically unstable near Nyquist and the
 * spec (§4.1 v3) requires clamping rather than throwing.
 */
export const EQ_NYQUIST_CEIL = 0.95;

/**
 * Clamp a center frequency into the safe RBJ range.
 *
 * @param freq requested center frequency in Hz
 * @param sampleRate audio sample rate in Hz
 * @returns clamped frequency in Hz
 */
export function clampFreq(freq: number, sampleRate: number): number {
  if (!Number.isFinite(freq) || freq < EQ_MIN_FREQ_HZ) return EQ_MIN_FREQ_HZ;
  const nyquist = sampleRate / 2;
  const ceil = EQ_NYQUIST_CEIL * nyquist;
  if (freq > ceil) return ceil;
  return freq;
}

/**
 * Compute RBJ peaking-filter coefficients (Audio EQ Cookbook).
 *
 * Reference: Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad
 * filter coefficients". Q matches the existing BiquadFilterNode usage
 * (default Q = 1/sqrt(2)). With gainDb = 0 (A=1) the numerator equals the
 * denominator (b0=1, b1=a1, b2=a2) so H(z) ≡ 1 — an all-pass. The
 * individual coefficients b1/b2/a1/a2 are NOT zero; the all-pass property
 * comes from num ≡ den, not from zero coefficients.
 *
 * @param freq center frequency in Hz (clamped to [20, 0.95 * sr/2])
 * @param gainDb boost/cut in dB
 * @param Q quality factor (filter sharpness)
 * @param sampleRate audio sample rate in Hz (NOT hardcoded to 48000)
 */
export function computePeakingCoeffs(
  freq: number,
  gainDb: number,
  Q: number,
  sampleRate: number,
): BiquadCoeffs {
  const f = clampFreq(freq, sampleRate);
  const A = Math.pow(10, gainDb / 40); // peaking: amplitude in linear gain / 2
  const w0 = (2 * Math.PI * f) / sampleRate;
  const cosW0 = Math.cos(w0);
  const sinW0 = Math.sin(w0);
  // alpha = sin(w0) / (2 * Q)
  const safeQ = Q > 0 ? Q : 1 / Math.SQRT2;
  const alpha = sinW0 / (2 * safeQ);

  // RBJ peaking:
  //   b0 = 1 + alpha*A
  //   b1 = -2*cosW0
  //   b2 = 1 - alpha*A
  //   a0 = 1 + alpha/A
  //   a1 = -2*cosW0
  //   a2 = 1 - alpha/A
  // Normalize by a0.
  const a0 = 1 + alpha / A;

  return {
    b0: (1 + alpha * A) / a0,
    b1: (-2 * cosW0) / a0,
    b2: (1 - alpha * A) / a0,
    a1: (-2 * cosW0) / a0,
    a2: (1 - alpha / A) / a0,
  };
}

/**
 * Run a single sample through one biquad stage (Direct Form I Transposed).
 *
 * Updates `state` in place and returns the output sample. The transposed
 * form is numerically stable for cascaded peaking filters and is the form
 * used by Web Audio's BiquadFilterNode internally.
 *
 * @param state mutable per-stage, per-channel state
 * @param sample input sample
 * @param coeffs normalized biquad coefficients (a0 === 1)
 */
export function cascadeBiquad(
  state: BiquadState,
  sample: number,
  coeffs: BiquadCoeffs,
): number {
  // Direct Form I Transposed:
  //   y[n] = b0*x[n] + s1
  //   s1   = b1*x[n] - a1*y[n] + s2
  //   s2   = b2*x[n] - a2*y[n]
  const { b0, b1, b2, a1, a2 } = coeffs;
  const y = b0 * sample + state.x1;
  state.x1 = b1 * sample - a1 * y + state.x2;
  state.x2 = b2 * sample - a2 * y;
  return y;
}

// ---------------------------------------------------------------------------
// AudioWorkletProcessor source string
// ---------------------------------------------------------------------------

/**
 * The AudioWorkletProcessor registration source, as a string.
 *
 * Why a string + Blob URL: Vite's `?worklet` suffix was verified unusable in
 * the Phase 0 spike (transform 500 — see spec §2.2.1 Check 2). Bundling the
 * processor as a string and loading it via `URL.createObjectURL(new Blob([src]))`
 * is the approach approved in the plan.
 *
 * The processor:
 * - maintains 10 biquad stages × 2 channels (stereo) of state
 * - listens on `port` for `{type:'setBands', bands}` and `{type:'setEnabled', enabled}`
 * - dezippers each band's coefficients (current → target at 0.1/block,
 *   ≈50ms settling at 128-sample blocks / 48kHz) to avoid clicks when the
 *   user drags a slider
 * - uses `globalThis.sampleRate` (the AudioWorkletScope built-in) so the
 *   coefficients are correct on 44100 Hz systems too (spec §4.1 v3)
 * - returns `true` from `process()` to keep the node alive
 *
 * The source is intentionally self-contained (no imports) so it can be
 * stringified into a Blob and loaded verbatim in the AudioWorklet global
 * scope, which has no access to the main module graph.
 */
export const EQ_PROCESSOR_SOURCE = `
// EqProcessor: 10-band peaking EQ cascade, running inside an AudioWorklet.
// Generated from ui/src/playback/eq/eqWorkletProcessor.ts — see that file for the
// pure-DSP unit tests. This string is loaded via Blob URL (see loadEqWorklet).

const NUM_BANDS = 10;
const NUM_CHANNELS = 2;
// Convergence rate per render quantum (128 samples). At 48kHz / 128-sample
// blocks (≈2.67ms/block), 0.1/block reaches ~99% of target in ~50ms — fast
// enough to feel responsive, slow enough to avoid the click a hard switch
// would produce on slider drag.
const DEZIPPER_RATE = 0.1;

function clampFreq(freq, sampleRate) {
  if (!Number.isFinite(freq) || freq < 20) return 20;
  var ceil = 0.95 * (sampleRate / 2);
  return freq > ceil ? ceil : freq;
}

function computePeakingCoeffs(freq, gainDb, Q, sampleRate) {
  var f = clampFreq(freq, sampleRate);
  var A = Math.pow(10, gainDb / 40);
  var w0 = (2 * Math.PI * f) / sampleRate;
  var cosW0 = Math.cos(w0);
  var sinW0 = Math.sin(w0);
  var safeQ = Q > 0 ? Q : 1 / Math.SQRT2;
  var alpha = sinW0 / (2 * safeQ);
  var a0 = 1 + alpha / A;
  return {
    b0: (1 + alpha * A) / a0,
    b1: (-2 * cosW0) / a0,
    b2: (1 - alpha * A) / a0,
    a1: (-2 * cosW0) / a0,
    a2: (1 - alpha / A) / a0,
  };
}

function cascadeBiquad(state, sample, coeffs) {
  // Direct Form I Transposed.
  var y = coeffs.b0 * sample + state.x1;
  state.x1 = coeffs.b1 * sample - coeffs.a1 * y + state.x2;
  state.x2 = coeffs.b2 * sample - coeffs.a2 * y;
  return y;
}

function zeroState() { return { x1: 0, x2: 0, y1: 0, y2: 0 }; }

function cloneCoeffs(c) {
  return { b0: c.b0, b1: c.b1, b2: c.b2, a1: c.a1, a2: c.a2 };
}

class EqProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    // 10 stages × 2 channels of DF1T state.
    this.states = [];
    for (var st = 0; st < NUM_BANDS; st++) {
      this.states.push([zeroState(), zeroState()]);
    }
    // Per-stage dezipper: current (what we apply this block) and target
    // (what setBands requested). Each block, current moves 10% toward target.
    var sr = globalThis.sampleRate;
    this.bandFreqs = ${JSON.stringify(EQ_BANDS.map((b) => b.frequency))};
    this.Q = 1 / Math.SQRT2;
    this.enabled = true;
    // Start at flat (gainDb = 0 → all-pass) so the first block is transparent.
    this.current = [];
    this.target = [];
    for (var i = 0; i < NUM_BANDS; i++) {
      var flat = computePeakingCoeffs(this.bandFreqs[i], 0, this.Q, sr);
      this.current.push(cloneCoeffs(flat));
      this.target.push(cloneCoeffs(flat));
    }
    var self = this;
    this.port.onmessage = function (e) {
      var msg = e.data;
      if (!msg) return;
      if (msg.type === 'setBands') {
        var bands = msg.bands;
        if (Array.isArray(bands)) {
          for (var j = 0; j < NUM_BANDS; j++) {
            var g = Number(bands[j] || 0);
            if (!Number.isFinite(g)) g = 0;
            // Clamp to [-6, +6] dB (matches EQ_MIN/EQ_MAX_GAIN_DB).
            if (g < -6) g = -6;
            if (g > 6) g = 6;
            var c = computePeakingCoeffs(self.bandFreqs[j], g, self.Q, globalThis.sampleRate);
            self.target[j] = c;
          }
        }
      } else if (msg.type === 'setEnabled') {
        self.enabled = !!msg.enabled;
      }
    };
  }

  process(inputs, outputs) {
    var input = inputs[0];
    var output = outputs[0];
    if (!input || input.length === 0 || !output || output.length === 0) {
      return true;
    }
    var numChannels = output.length < NUM_CHANNELS ? output.length : NUM_CHANNELS;
    var blockSize = output[0].length;

    // Dezipper: move each band's current coeffs 10% toward target this block.
    // When disabled, target is all-pass (flat) — bypassing without rebuilding
    // the cascade, so re-enabling is instantaneous from the user's POV but
    // still click-free.
    for (var b = 0; b < NUM_BANDS; b++) {
      var tgt = this.enabled ? this.target[b] : computePeakingCoeffs(this.bandFreqs[b], 0, this.Q, globalThis.sampleRate);
      var cur = this.current[b];
      cur.b0 += DEZIPPER_RATE * (tgt.b0 - cur.b0);
      cur.b1 += DEZIPPER_RATE * (tgt.b1 - cur.b1);
      cur.b2 += DEZIPPER_RATE * (tgt.b2 - cur.b2);
      cur.a1 += DEZIPPER_RATE * (tgt.a1 - cur.a1);
      cur.a2 += DEZIPPER_RATE * (tgt.a2 - cur.a2);
    }

    for (var ch = 0; ch < numChannels; ch++) {
      var inCh = input[ch] || input[0];
      var outCh = output[ch];
      for (var i = 0; i < blockSize; i++) {
        var s = inCh[i];
        for (var st = 0; st < NUM_BANDS; st++) {
          s = cascadeBiquad(this.states[st][ch], s, this.current[st]);
        }
        outCh[i] = s;
      }
    }
    return true;
  }
}

registerProcessor('eq-processor', EqProcessor);
`;

// ---------------------------------------------------------------------------
// Blob URL loader (Step 1.4)
// ---------------------------------------------------------------------------

/**
 * Error thrown when `audioCtx.audioWorklet.addModule` rejects while loading
 * the EQ processor from a Blob URL. The original error is attached as
 * `cause` so callers can inspect the underlying CSP / network failure.
 *
 * `Error.cause` is an ES2022 feature; the project's `tsconfig.json` targets
 * ES2020, so we declare the field explicitly and forward the cause manually
 * rather than relying on the built-in `Error` options bag.
 */
export class WorkletLoadError extends Error {
  /** The underlying error that caused the worklet load to fail. */
  public readonly cause?: unknown;
  constructor(message: string, options?: { cause?: unknown }) {
    super(message);
    this.name = 'WorkletLoadError';
    if (options && 'cause' in options) {
      this.cause = options.cause;
    }
  }
}

/**
 * Minimum shape of AudioContext we need to load a worklet. Keeping this narrow
 * lets Phase 2 tests pass a mock without constructing a full AudioContext.
 */
export interface AudioContextForWorklet {
  readonly audioWorklet: {
    addModule(url: string): Promise<void>;
  };
}

/**
 * Load the EQ AudioWorkletProcessor into `audioCtx` via a Blob URL.
 *
 * @returns the Blob URL string. The caller SHOULD call `URL.revokeObjectURL`
 *   on it once the worklet has been registered (or on teardown). On failure
 *   the URL is revoked before throwing, so the caller does not need to.
 * @throws {WorkletLoadError} if `addModule` rejects, with the original error
 *   attached as `cause`.
 */
export async function loadEqWorklet(audioCtx: AudioContextForWorklet): Promise<string> {
  const blob = new Blob([EQ_PROCESSOR_SOURCE], { type: 'text/javascript' });
  const url = URL.createObjectURL(blob);
  try {
    await audioCtx.audioWorklet.addModule(url);
    return url;
  } catch (err) {
    URL.revokeObjectURL(url);
    throw new WorkletLoadError('Failed to load EQ AudioWorklet module', {
      cause: err,
    });
  }
}

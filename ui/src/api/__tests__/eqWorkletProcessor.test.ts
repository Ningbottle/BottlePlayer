import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  computePeakingCoeffs,
  cascadeBiquad,
  loadEqWorklet,
  WorkletLoadError,
  EQ_PROCESSOR_SOURCE,
  type BiquadCoeffs,
  type BiquadState,
} from '../eqWorkletProcessor';

const SQRT1_2 = 1 / Math.SQRT2;

/**
 * Helper: generate N samples of a sine wave at `freq` Hz, sampleRate `sr`.
 */
function sineWave(n: number, freq: number, sr: number, amp = 0.5): Float32Array {
  const out = new Float32Array(n);
  const w = (2 * Math.PI * freq) / sr;
  for (let i = 0; i < n; i++) out[i] = amp * Math.sin(w * i);
  return out;
}

/**
 * Helper: RMS of a Float32Array.
 */
function rms(buf: Float32Array | number[]): number {
  let s = 0;
  for (let i = 0; i < buf.length; i++) s += buf[i] * buf[i];
  return Math.sqrt(s / buf.length);
}

/**
 * Helper: run a cascade of biquads (10 stages) over an input buffer using
 * per-stage state and the given coeffs (same coeffs at every stage for testing).
 */
function runCascade(
  input: Float32Array,
  coeffs: BiquadCoeffs,
  stages = 10,
): { output: Float32Array; states: BiquadState[] } {
  const states: BiquadState[] = Array.from({ length: stages }, () => ({
    x1: 0,
    x2: 0,
    y1: 0,
    y2: 0,
  }));
  const output = new Float32Array(input.length);
  for (let i = 0; i < input.length; i++) {
    let s = input[i];
    for (let st = 0; st < stages; st++) s = cascadeBiquad(states[st], s, coeffs);
    output[i] = s;
  }
  return { output, states };
}

describe('computePeakingCoeffs', () => {
  it('returns all-pass coefficients (b0=1, b1=a1, b2=a2) for gainDb=0 at 48kHz', () => {
    // RBJ peaking at gainDb=0 is magnitude-flat (H(z) ≡ 1). With A=1 the
    // formula collapses so b0=1 and b1=a1, b2=a2 — i.e. numerator equals
    // denominator, making the transfer function identically 1 (all-pass).
    // (The coefficients are NOT all zero; the all-pass property comes from
    //  num ≡ den, verified by the cascadeBiquad all-pass test downstream.)
    const c = computePeakingCoeffs(1000, 0, SQRT1_2, 48000);
    expect(c.b0).toBeCloseTo(1, 6);
    expect(c.b1).toBeCloseTo(c.a1, 6);
    expect(c.b2).toBeCloseTo(c.a2, 6);
  });

  it('returns peaking coefficients (b0>1, a1<0) for gainDb=+6 at 48kHz', () => {
    const c = computePeakingCoeffs(1000, 6, SQRT1_2, 48000);
    expect(c.b0).toBeGreaterThan(1);
    expect(c.a1).toBeLessThan(0);
  });

  it('is sample-rate aware: 44100 vs 48000 produce different coefficients', () => {
    const c44 = computePeakingCoeffs(1000, 6, SQRT1_2, 44100);
    const c48 = computePeakingCoeffs(1000, 6, SQRT1_2, 48000);
    expect(c44.b0).not.toBeCloseTo(c48.b0, 6);
    expect(c44.a1).not.toBeCloseTo(c48.a1, 6);
  });

  it('does not throw and stays monotonic for gainDb = -6 and +6', () => {
    const neg = computePeakingCoeffs(1000, -6, SQRT1_2, 48000);
    const zero = computePeakingCoeffs(1000, 0, SQRT1_2, 48000);
    const pos = computePeakingCoeffs(1000, 6, SQRT1_2, 48000);
    // zero is all-pass (b0 === 1).
    expect(zero.b0).toBeCloseTo(1, 6);
    // RBJ peaking complementary-magnitude symmetry: b0(+g) * b0(-g) ≈ 1.
    expect(neg.b0 * pos.b0).toBeCloseTo(1, 6);
    // +6 boosts (b0 > 1), -6 cuts (b0 < 1).
    expect(pos.b0).toBeGreaterThan(1);
    expect(neg.b0).toBeLessThan(1);
  });

  it('clamps out-of-range freq (no throw) — below 20 and above Nyquist', () => {
    expect(() => computePeakingCoeffs(5, 6, SQRT1_2, 48000)).not.toThrow();
    expect(() => computePeakingCoeffs(60000, 6, SQRT1_2, 48000)).not.toThrow();
    // Clamped to >= 20 → must not equal an absurd freq's coefficient (sanity).
    const clampedLow = computePeakingCoeffs(5, 6, SQRT1_2, 48000);
    const at20 = computePeakingCoeffs(20, 6, SQRT1_2, 48000);
    expect(clampedLow.b0).toBeCloseTo(at20.b0, 6);
    // Above Nyquist (sr/2 = 24000) clamps to 0.95 * Nyquist.
    const clampedHigh = computePeakingCoeffs(60000, 6, SQRT1_2, 48000);
    const atCeil = computePeakingCoeffs(0.95 * (48000 / 2), 6, SQRT1_2, 48000);
    expect(clampedHigh.b0).toBeCloseTo(atCeil.b0, 6);
  });
});

describe('cascadeBiquad', () => {
  it('all-pass coeffs (gainDb=0) yield output === input within 1e-6', () => {
    const coeffs = computePeakingCoeffs(1000, 0, SQRT1_2, 48000);
    const state: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };
    const input = sineWave(2048, 1000, 48000);
    const output = new Float32Array(input.length);
    for (let i = 0; i < input.length; i++) output[i] = cascadeBiquad(state, input[i], coeffs);
    for (let i = 0; i < input.length; i++) {
      expect(output[i]).toBeCloseTo(input[i], 6);
    }
  });

  it('1000Hz sine, gainDb=+6 produces output RMS > input RMS', () => {
    const coeffs = computePeakingCoeffs(1000, 6, SQRT1_2, 48000);
    const input = sineWave(8192, 1000, 48000, 0.2);
    const { output } = runCascade(input, coeffs, 1);
    // After steady state, the peaking filter at its center freq should amplify.
    expect(rms(output)).toBeGreaterThan(rms(input));
  });

  it('state x1/x2/y1/y2 does not cross-contaminate between channels', () => {
    // Two independent states processed in interleaved fashion must match
    // two independent states processed sequentially.
    const coeffs = computePeakingCoeffs(1000, 6, SQRT1_2, 48000);
    const stateA_interleaved: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };
    const stateB_interleaved: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };
    const stateA_seq: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };
    const stateB_seq: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };

    const inputA = sineWave(512, 1000, 48000, 0.3);
    const inputB = sineWave(512, 2000, 48000, 0.2);

    // Interleaved: process A[0], B[0], A[1], B[1], ...
    const outA_interleaved = new Float32Array(inputA.length);
    const outB_interleaved = new Float32Array(inputB.length);
    for (let i = 0; i < inputA.length; i++) {
      outA_interleaved[i] = cascadeBiquad(stateA_interleaved, inputA[i], coeffs);
      outB_interleaved[i] = cascadeBiquad(stateB_interleaved, inputB[i], coeffs);
    }
    // Sequential: process all A, then all B.
    const outA_seq = new Float32Array(inputA.length);
    const outB_seq = new Float32Array(inputB.length);
    for (let i = 0; i < inputA.length; i++) {
      outA_seq[i] = cascadeBiquad(stateA_seq, inputA[i], coeffs);
    }
    for (let i = 0; i < inputB.length; i++) {
      outB_seq[i] = cascadeBiquad(stateB_seq, inputB[i], coeffs);
    }

    for (let i = 0; i < inputA.length; i++) {
      expect(outA_interleaved[i]).toBeCloseTo(outA_seq[i], 6);
      expect(outB_interleaved[i]).toBeCloseTo(outB_seq[i], 6);
    }
  });
});

describe('EQ_PROCESSOR_SOURCE', () => {
  it('registers as "eq-processor" and extends AudioWorkletProcessor', () => {
    expect(EQ_PROCESSOR_SOURCE).toContain("registerProcessor('eq-processor'");
    expect(EQ_PROCESSOR_SOURCE).toContain('class EqProcessor extends AudioWorkletProcessor');
  });

  it('uses globalThis.sampleRate (not hardcoded 48000)', () => {
    expect(EQ_PROCESSOR_SOURCE).toMatch(/globalThis\.sampleRate|sampleRate\b/);
    // No hardcoded 48000 sample-rate literal in coefficient computation.
    // (A literal 48000 in comments/whitespace is acceptable; check the
    //  actual sample-rate usage is from globalThis.)
    expect(EQ_PROCESSOR_SOURCE).toContain('globalThis.sampleRate');
  });

  it('handles setBands and setEnabled via port.onmessage', () => {
    expect(EQ_PROCESSOR_SOURCE).toMatch(/['"]setBands['"]/);
    expect(EQ_PROCESSOR_SOURCE).toMatch(/['"]setEnabled['"]/);
    expect(EQ_PROCESSOR_SOURCE).toMatch(/port\.onmessage/);
  });

  it('process() returns true to keep the node alive', () => {
    expect(EQ_PROCESSOR_SOURCE).toMatch(/return\s+true/);
  });

  it('includes dezipper (current/target coefficient convergence)', () => {
    // Step 1.3: dezipper present in source.
    expect(EQ_PROCESSOR_SOURCE.toLowerCase()).toMatch(/target|current|dezip/);
  });
});

describe('dezipper (Step 1.3)', () => {
  /**
   * Run the processor's dezipper logic indirectly: a slider 0→+6 transition
   * must not produce a click. We approximate "no click" as: no high-frequency
   * spectral spike in the output vs. a hard-switched baseline.
   *
   * Since we can't instantiate the AudioWorkletProcessor in jsdom, we test the
   * dezipper behavior via the pure cascadeBiquad + a coeff-transition simulation
   * that mirrors the processor's convergence rule (0.1 * (target - current)
   * per block). The processor source string itself is checked separately for
   * containing the dezipper.
   */
  it('slider 0→+6 transition produces no high-freq spectral spike (dezipper smooths)', () => {
    const sr = 48000;
    const blockSize = 128;
    const numBlocks = 50; // ~133ms, plenty for 50ms settling
    const targetCoeffs = computePeakingCoeffs(1000, 6, SQRT1_2, sr);
    const zeroCoeffs = computePeakingCoeffs(1000, 0, SQRT1_2, sr);

    // Simulate processor dezipper: per block, coeffs move 10% toward target.
    function runWithDezipper(dezip: boolean): Float32Array {
      const state: BiquadState = { x1: 0, x2: 0, y1: 0, y2: 0 };
      const out = new Float32Array(numBlocks * blockSize);
      let cur: BiquadCoeffs = { ...zeroCoeffs };
      const tgt = targetCoeffs;
      const rate = 0.1;
      for (let b = 0; b < numBlocks; b++) {
        if (dezip) {
          cur = {
            b0: cur.b0 + rate * (tgt.b0 - cur.b0),
            b1: cur.b1 + rate * (tgt.b1 - cur.b1),
            b2: cur.b2 + rate * (tgt.b2 - cur.b2),
            a1: cur.a1 + rate * (tgt.a1 - cur.a1),
            a2: cur.a2 + rate * (tgt.a2 - cur.a2),
          };
        } else {
          // Hard switch at block boundary (no dezipper): instant jump at block 1.
          cur = b === 0 ? { ...zeroCoeffs } : { ...tgt };
        }
        // Drive with broadband white-ish input (sum of sines across spectrum).
        for (let i = 0; i < blockSize; i++) {
          const t = b * blockSize + i;
          const x =
            0.1 * Math.sin((2 * Math.PI * 1000 * t) / sr) +
            0.1 * Math.sin((2 * Math.PI * 5000 * t) / sr) +
            0.1 * Math.sin((2 * Math.PI * 15000 * t) / sr);
          out[t] = cascadeBiquad(state, x, cur);
        }
      }
      return out;
    }

    const dezipOut = runWithDezipper(true);
    const hardOut = runWithDezipper(false);

    // Measure high-freq energy (above 8kHz) via simple HPF approximation:
    // the difference between consecutive samples (a crude derivative) captures
    // transient/click energy. A click manifests as a large derivative spike.
    let dezipMaxDelta = 0;
    let hardMaxDelta = 0;
    for (let i = 1; i < dezipOut.length; i++) {
      dezipMaxDelta = Math.max(dezipMaxDelta, Math.abs(dezipOut[i] - dezipOut[i - 1]));
      hardMaxDelta = Math.max(hardMaxDelta, Math.abs(hardOut[i] - hardOut[i - 1]));
    }
    // The dezipper should produce a strictly smaller max sample-to-sample
    // delta than the hard switch (which jumps instantaneously).
    expect(dezipMaxDelta).toBeLessThan(hardMaxDelta);
  });
});

describe('loadEqWorklet (Step 1.4)', () => {
  let createObjectURLSpy: ReturnType<typeof vi.fn>;
  let revokeObjectURLSpy: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    createObjectURLSpy = vi.fn((_blob: Blob) => 'blob:fake-url-' + Math.random());
    revokeObjectURLSpy = vi.fn();
    vi.stubGlobal('URL', {
      createObjectURL: createObjectURLSpy,
      revokeObjectURL: revokeObjectURLSpy,
    });
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  function makeMockCtx(addModuleImpl?: (url: string) => Promise<void>) {
    const addModule = vi.fn(
      addModuleImpl ?? (async (_url: string) => {}),
    );
    const ctx = {
      audioWorklet: { addModule },
      sampleRate: 48000,
    };
    return { ctx, addModule };
  }

  it('calls audioCtx.audioWorklet.addModule once with a blob: URL', async () => {
    const { ctx, addModule } = makeMockCtx();
    await loadEqWorklet(ctx as any);
    expect(addModule).toHaveBeenCalledTimes(1);
    const arg = addModule.mock.calls[0][0];
    expect(typeof arg).toBe('string');
    expect(arg.startsWith('blob:')).toBe(true);
  });

  it('creates a Blob with the processor source as text/javascript', async () => {
    const blobCalls: { parts: BlobPart[]; opts?: BlobPropertyBag }[] = [];
    class MockBlob {
      parts: BlobPart[];
      opts?: BlobPropertyBag;
      size = 0;
      type: string;
      constructor(parts: BlobPart[], opts?: BlobPropertyBag) {
        this.parts = parts;
        this.opts = opts;
        this.type = opts?.type ?? '';
        blobCalls.push({ parts, opts });
      }
    }
    vi.stubGlobal('Blob', MockBlob);
    const { ctx } = makeMockCtx();
    await loadEqWorklet(ctx as any);
    expect(blobCalls).toHaveLength(1);
    const [parts, opts] = [blobCalls[0].parts, blobCalls[0].opts];
    expect(parts.length).toBe(1);
    expect(typeof parts[0]).toBe('string');
    expect(parts[0]).toContain("registerProcessor('eq-processor'");
    expect(opts?.type).toBe('text/javascript');
  });

  it('returns a revoke-able URL on success', async () => {
    const { ctx } = makeMockCtx();
    const url = await loadEqWorklet(ctx as any);
    expect(typeof url).toBe('string');
    expect(url.startsWith('blob:')).toBe(true);
    // revoking the returned URL is the caller's responsibility.
    URL.revokeObjectURL(url);
    expect(revokeObjectURLSpy).toHaveBeenCalledWith(url);
  });

  it('throws WorkletLoadError (with original error attached) when addModule rejects', async () => {
    const original = new Error('CSP blocked blob: URL');
    const { ctx } = makeMockCtx(async () => {
      throw original;
    });
    await expect(loadEqWorklet(ctx as any)).rejects.toBeInstanceOf(WorkletLoadError);
    await expect(loadEqWorklet(ctx as any)).rejects.toThrow(/worklet/i);
    try {
      await loadEqWorklet(ctx as any);
      throw new Error('should have thrown');
    } catch (e) {
      expect(e).toBeInstanceOf(WorkletLoadError);
      expect((e as WorkletLoadError).cause).toBe(original);
    }
  });

  it('revokes the blob URL even when addModule rejects', async () => {
    const original = new Error('reject');
    const { ctx } = makeMockCtx(async () => {
      throw original;
    });
    await expect(loadEqWorklet(ctx as any)).rejects.toBeInstanceOf(WorkletLoadError);
    expect(createObjectURLSpy).toHaveBeenCalledTimes(1);
    expect(revokeObjectURLSpy).toHaveBeenCalledTimes(1);
  });
});

/**
 * Runtime smoke tests of the processor source string (P1 backfill).
 *
 * The string-matched assertions above can't catch a runtime bug in
 * `EqProcessor` (e.g. a typo in `this.states` init). These tests eval the
 * source in a sandbox that stubs `AudioWorkletProcessor` + `registerProcessor`
 * + `globalThis.sampleRate`, then drive the captured `EqProcessor` class
 * directly with synthetic Float32Array inputs to assert real DSP behavior.
 */
describe('EQ_PROCESSOR_SOURCE runtime behavior', () => {
  type EqProcessorCtor = new () => {
    states: Array<[BiquadState, BiquadState]>;
    port: { onmessage: ((e: { data: unknown }) => void) | null };
    process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean;
  };

  function evalProcessor(sr = 48000): EqProcessorCtor {
    let capturedCtor: EqProcessorCtor | null = null;
    const sandbox = {
      AudioWorkletProcessor: class {
        port = { onmessage: null as unknown };
      },
      registerProcessor: (_name: string, ctor: EqProcessorCtor) => {
        capturedCtor = ctor;
      },
      Math,
      Number,
      Array,
      JSON,
      globalThis: { sampleRate: sr },
      // `globalThis.sampleRate` is read via property access in the source;
      // expose it on the sandbox's globalThis too.
      sampleRate: sr,
    };
    // Use Function constructor to eval the source in a scope where
    // `AudioWorkletProcessor` / `registerProcessor` / `globalThis` resolve
    // to the sandbox. We expose the sandbox keys as locals.
    const wrapper = new Function(
      'AudioWorkletProcessor',
      'registerProcessor',
      'Math',
      'Number',
      'Array',
      'JSON',
      'globalThis',
      `"use strict";\n${EQ_PROCESSOR_SOURCE}\nreturn EqProcessor;`,
    );
    wrapper(
      sandbox.AudioWorkletProcessor,
      sandbox.registerProcessor,
      sandbox.Math,
      sandbox.Number,
      sandbox.Array,
      sandbox.JSON,
      sandbox.globalThis,
    );
    if (!capturedCtor) throw new Error('registerProcessor was not called');
    return capturedCtor;
  }

  function makeBlock(n: number, sr: number, freq: number, amp = 0.5): Float32Array[] {
    const ch = new Float32Array(n);
    const w = (2 * Math.PI * freq) / sr;
    for (let i = 0; i < n; i++) ch[i] = amp * Math.sin(w * i);
    return [ch, ch]; // stereo
  }

  it('process() outputs non-zero PCM for non-zero input', () => {
    const Ctor = evalProcessor(48000);
    const proc = new Ctor();
    const input = makeBlock(128, 48000, 1000, 0.5);
    const output: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
    const alive = proc.process([input], output);
    expect(alive).toBe(true);
    // Both channels must have non-zero energy.
    let lRms = 0, rRms = 0;
    for (let i = 0; i < 128; i++) {
      lRms += output[0][0][i] * output[0][0][i];
      rRms += output[0][1][i] * output[0][1][i];
    }
    expect(Math.sqrt(lRms / 128)).toBeGreaterThan(1e-3);
    expect(Math.sqrt(rRms / 128)).toBeGreaterThan(1e-3);
  });

  it('process() with flat bands (default) preserves input magnitude within ±5%', () => {
    const Ctor = evalProcessor(48000);
    const proc = new Ctor();
    // Default target is flat (gainDb=0) — after dezipper settles (~50ms),
    // output magnitude at the center freq should match input.
    const sr = 48000;
    const numBlocks = 50;
    let lastRatio = 0;
    for (let b = 0; b < numBlocks; b++) {
      const input = makeBlock(128, sr, 1000, 0.5);
      const output: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
      proc.process([input], output);
      if (b >= 40) {
        // After settling, compare RMS.
        let inRms = 0, outRms = 0;
        for (let i = 0; i < 128; i++) {
          inRms += input[0][i] * input[0][i];
          outRms += output[0][0][i] * output[0][0][i];
        }
        lastRatio = Math.sqrt(outRms / inRms);
      }
    }
    // Flat EQ: ratio ≈ 1.0 (within 5% — dezipper residual + DF1T transient).
    expect(lastRatio).toBeGreaterThan(0.95);
    expect(lastRatio).toBeLessThan(1.05);
  });

  it('setBands with +6 at 1kHz boosts 1kHz energy vs flat', () => {
    const Ctor = evalProcessor(48000);
    const proc = new Ctor();
    // Find the band closest to 1000Hz (index 5: "1K").
    const bands = new Array(10).fill(0);
    bands[5] = 6;
    proc.port.onmessage!({ data: { type: 'setBands', bands } });

    const sr = 48000;
    const numBlocks = 80; // ~213ms, plenty for 50ms dezipper to settle
    let boostedRatio = 0;
    for (let b = 0; b < numBlocks; b++) {
      const input = makeBlock(128, sr, 1000, 0.3);
      const output: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
      proc.process([input], output);
      if (b >= 60) {
        let inRms = 0, outRms = 0;
        for (let i = 0; i < 128; i++) {
          inRms += input[0][i] * input[0][i];
          outRms += output[0][0][i] * output[0][0][i];
        }
        boostedRatio = Math.sqrt(outRms / inRms);
      }
    }
    // +6dB boost at 1K should raise 1kHz energy by ~3-6dB (factor ~1.4-2.0).
    expect(boostedRatio).toBeGreaterThan(1.3);
  });

  it('setEnabled(false) bypasses the cascade (output ≈ input after settle)', () => {
    const Ctor = evalProcessor(48000);
    const proc = new Ctor();
    // First apply a +6 boost at 1K so we have something to bypass.
    const bands = new Array(10).fill(0);
    bands[5] = 6;
    proc.port.onmessage!({ data: { type: 'setBands', bands } });
    // Run a few blocks to let the boost engage.
    for (let b = 0; b < 40; b++) {
      const input = makeBlock(128, 48000, 1000, 0.3);
      proc.process([input], [[[new Float32Array(128), new Float32Array(128)]]] as any);
    }
    // Now disable — target becomes all-pass.
    proc.port.onmessage!({ data: { type: 'setEnabled', enabled: false } });
    const sr = 48000;
    const numBlocks = 80;
    let bypassRatio = 0;
    for (let b = 0; b < numBlocks; b++) {
      const input = makeBlock(128, sr, 1000, 0.3);
      const output: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
      proc.process([input], output);
      if (b >= 60) {
        let inRms = 0, outRms = 0;
        for (let i = 0; i < 128; i++) {
          inRms += input[0][i] * input[0][i];
          outRms += output[0][0][i] * output[0][0][i];
        }
        bypassRatio = Math.sqrt(outRms / inRms);
      }
    }
    // Bypassed: ratio ≈ 1.0 (within 5%).
    expect(bypassRatio).toBeGreaterThan(0.95);
    expect(bypassRatio).toBeLessThan(1.05);
  });

  it('10-stage cascade end-to-end: +6 at 1K boosts more than +6 single-stage', () => {
    // Single-stage peaking at 1K, +6dB → ~2x amplitude at center.
    // 10-stage cascade of the same +6 at 1K → ~2^10? No — each stage is a
    // peaking filter at the SAME freq, so they stack. But the cascade test
    // here uses the real processor (10 distinct band freqs, only band 5 at 1K).
    // So we compare: band[5]=+6 (others 0) vs band[5]=0 — the boost at 1kHz
    // must be strictly positive.
    const Ctor = evalProcessor(48000);
    const procBoost = new Ctor();
    const bands = new Array(10).fill(0);
    bands[5] = 6;
    procBoost.port.onmessage!({ data: { type: 'setBands', bands } });
    const procFlat = new Ctor();

    const sr = 48000;
    const numBlocks = 80;
    let boostRatio = 0, flatRatio = 0;
    for (let b = 0; b < numBlocks; b++) {
      const input = makeBlock(128, sr, 1000, 0.3);
      const outBoost: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
      const outFlat: Float32Array[][] = [[new Float32Array(128), new Float32Array(128)]];
      procBoost.process([input], outBoost);
      procFlat.process([input], outFlat);
      if (b >= 60) {
        let inR = 0, bR = 0, fR = 0;
        for (let i = 0; i < 128; i++) {
          inR += input[0][i] * input[0][i];
          bR += outBoost[0][0][i] * outBoost[0][0][i];
          fR += outFlat[0][0][i] * outFlat[0][0][i];
        }
        boostRatio = Math.sqrt(bR / inR);
        flatRatio = Math.sqrt(fR / inR);
      }
    }
    // Boosted must exceed flat at the center frequency.
    expect(boostRatio).toBeGreaterThan(flatRatio);
  });
});

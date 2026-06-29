import { describe, it, expect, vi } from 'vitest';
import { WebAudioEq } from '../webAudioEq';

/**
 * A minimal AudioContext mock that records node creation and connection order.
 * jsdom has no Web Audio API, so the EQ controller must be drivable from a
 * pluggable context factory in tests.
 */
function makeMockContext() {
  const calls: string[] = [];
  const filters: any[] = [];
  const connectTo = (selfName: string) => ({
    connect: (node: any) => {
      calls.push(`${selfName}->${node.__name}`);
      return node;
    },
  });
  const mkNode = (name: string, extra: any = {}) => ({
    __name: name,
    ...connectTo(name),
    ...extra,
  });

  let biquadCount = 0;
  const ctx: any = {
    __name: 'destination',
    state: 'suspended',
    resume: vi.fn(async () => {
      ctx.state = 'running';
    }),
    close: vi.fn(async () => {
      ctx.state = 'closed';
    }),
    destination: mkNode('destination'),
    createMediaElementSource: vi.fn(() => mkNode('source')),
    createBiquadFilter: vi.fn(() => {
      biquadCount++;
      const filter = mkNode(`filter${biquadCount}`, {
        type: '',
        frequency: { value: 0 },
        Q: { value: 0 },
        gain: { value: 0 },
      });
      filters.push(filter);
      return filter;
    }),
    createGain: vi.fn(() => mkNode('gain', { gain: { value: 0 } })),
  };
  return { ctx, calls, filters };
}

describe('WebAudioEq', () => {
  it('builds the full filter/gain graph to destination BEFORE creating the source node', () => {
    // The #4 bug: createMediaElementSource was called before the connect chain,
    // so a mid-init throw orphaned the audio into a disconnected graph.
    const { ctx, calls } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: false,
      bands: [0, 0, 0, 0, 0],
      onSuspendedFail: () => {},
    });

    const sourceIdx = calls.indexOf('source->filter1');
    const lastFilterToGain = calls.indexOf('filter10->gain');
    const gainToDest = calls.indexOf('gain->destination');

    // graph to destination is wired before source connects to filter1
    expect(lastFilterToGain).toBeGreaterThanOrEqual(0);
    expect(gainToDest).toBeGreaterThan(lastFilterToGain);
    expect(sourceIdx).toBeGreaterThan(gainToDest);
  });

  it('does not call createMediaElementSource when the media URL is cross-origin non-CORS', () => {
    // The #1 bug: forcing crossOrigin='anonymous' on KuGou's non-CORS CDN breaks
    // playback entirely. When the source is cross-origin and not CORS-enabled,
    // EQ must be skipped (audio plays directly) rather than break the load.
    const { ctx } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [3, 0, 0, 0, 0],
      crossOriginSafe: false, // simulate non-CORS CDN
      onSuspendedFail: () => {},
    });
    expect(ctx.createMediaElementSource).not.toHaveBeenCalled();
  });

  it('calls createMediaElementSource when the media source is CORS-safe', () => {
    const { ctx } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [3, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    expect(ctx.createMediaElementSource).toHaveBeenCalledTimes(1);
  });

  it('close() closes the AudioContext and allows re-init on a new audio element', () => {
    // The #9 bug: AudioContext was never closed; HMR leaked contexts until the
    // browser cap was hit. close() must release it so a fresh context can be built.
    const { ctx } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    eq.close();
    expect(ctx.close).toHaveBeenCalled();
    // re-init on a new element builds a new source without throwing
    const { ctx: ctx2 } = makeMockContext();
    // a fresh WebAudioEq with a fresh context factory re-inits cleanly
    const eq2 = new WebAudioEq(() => ctx2);
    eq2.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    expect(ctx2.createMediaElementSource).toHaveBeenCalledTimes(1);
  });

  it('signals degraded passthrough when resume() rejects (suspended context)', async () => {
    // The #10 bug: resume() rejection was swallowed by .catch(()=>{}), leaving
    // the graph suspended (silence) while isPlaying stayed true. Now a failed
    // resume must notify the caller so it can degrade gracefully.
    const { ctx } = makeMockContext();
    ctx.resume = vi.fn(async () => {
      throw new Error('not allowed');
    });
    ctx.state = 'suspended';
    let degraded = false;
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => {
        degraded = true;
      },
    });
    await eq.resume();
    expect(degraded).toBe(true);
  });

  it('applies band gains to the biquad filters when enabled', () => {
    const { ctx, filters } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [5, -3, 0, 2, -1, 1, 2, -2, 3, -4],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    expect(ctx.createBiquadFilter).toHaveBeenCalledTimes(10);
    expect(filters.map(filter => filter.gain.value)).toEqual([5, -3, 0, 2, -1, 1, 2, -2, 3, -4]);
  });

  it('creates filters at the 10 reference frequencies', () => {
    const { ctx, filters } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [0, 0, 6, 0, 0, 0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    expect(filters.map(filter => filter.frequency.value)).toEqual([
      31,
      62,
      125,
      250,
      500,
      1000,
      2000,
      4000,
      8000,
      16000,
    ]);
  });

  // ── Regression: auto-advance wedge (#16) ──
  // CONFIRMED ROOT CAUSE (from browser F12 trace):
  // `createMediaElementSource(audio)` is irreversible for the element's
  // lifetime — calling it a SECOND time on the same element throws
  // InvalidStateError. The new audio-proxy + WebAudio EQ chain calls
  // `initEq` → `WebAudioEq.init` on EVERY `setPreparedSource` (every track
  // switch). Song 1 binds the element. Song 2's `init` either (a) no-ops via
  // the `if (this.ctx) return` guard if ctx survived, or (b) THROWS
  // InvalidStateError if ctx was nulled — and either way the element is
  // permanently bound to song 1's source node.
  //
  // The fix has two parts:
  //   1. WebAudioEq.init's catch block detects InvalidStateError and fires
  //      `onElementWedged` so the caller can swap in a fresh <audio> element.
  //      It keeps `this.ctx` set so subsequent init() calls on the SAME
  //      element short-circuit (don't re-throw on every track).
  //   2. playerStore.swapAudioElementAfterWedge() tears down the old element
  //      + backend, creates a fresh `new Audio()`, rebuilds the backend, and
  //      re-triggers the current track. A session flag `eqDisabledForSession`
  //      ensures the fresh element never calls createMediaElementSource
  //      again (EQ off for the rest of the session → no second wedge).
  //
  // The first test below pins the WebAudioEq-side behavior; the second pins
  // the suspended-context resume behavior (degraded but NOT torn down, so the
  // init guard holds).
  it('fires onElementWedged and keeps ctx set when createMediaElementSource throws InvalidStateError on an already-bound element', () => {
    const { ctx } = makeMockContext();
    // A minimal source node with a connect() stub (the graph code calls
    // source.connect(filters[0])).
    const sourceNode = { connect: vi.fn(() => sourceNode) };
    // Make the SECOND call to createMediaElementSource throw, simulating an
    // element that was already bound by song 1's successful call.
    let cmesCalls = 0;
    ctx.createMediaElementSource = vi.fn(() => {
      cmesCalls++;
      if (cmesCalls === 1) {
        // Song 1: succeeds, returns a source node (element becomes bound).
        return sourceNode;
      }
      // Song 2: element already bound → throws.
      const err = new Error("Failed to execute 'createMediaElementSource' on 'AudioContext': HTMLMediaElement already connected previously to a different MediaElementSourceNode.");
      (err as Error & { name: string }).name = 'InvalidStateError';
      throw err;
    });

    let wedged = false;
    const eq = new WebAudioEq(() => ctx);
    const audio = {} as HTMLAudioElement;

    // Song 1: graph builds successfully, element bound.
    eq.init(audio, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onElementWedged: () => { wedged = true; },
    });
    expect(eq.isRerouted).toBe(true);
    expect(wedged).toBe(false);

    // Simulate the ctx being nulled (e.g. by a prior buggy resume path) so
    // init's guard does NOT short-circuit on song 2. This is exactly the
    // condition that produced the F12 InvalidStateError trace.
    (eq as unknown as { ctx: unknown }).ctx = null;

    // Song 2: init re-runs, createMediaElementSource throws → onElementWedged.
    eq.init(audio, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onElementWedged: () => { wedged = true; },
    });
    expect(wedged, 'onElementWedged must fire when createMediaElementSource throws InvalidStateError').toBe(true);
    expect((eq as unknown as { ctx: unknown }).ctx, 'ctx must stay set so init guard holds and prevents re-throws on subsequent tracks').toBe(ctx);

    // Song 3: init on the same (still-bound) element must NOT re-throw — the
    // guard short-circuits. Verify by asserting createMediaElementSource is
    // not called a third time.
    const cmesBefore = (ctx.createMediaElementSource as ReturnType<typeof vi.fn>).mock.calls.length;
    eq.init(audio, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onElementWedged: () => { wedged = true; },
    });
    const cmesAfter = (ctx.createMediaElementSource as ReturnType<typeof vi.fn>).mock.calls.length;
    expect(cmesAfter, 'subsequent init() on the wedged element must short-circuit, not re-throw').toBe(cmesBefore);
  });

  it('surfaces onSuspendedFail on a failed resume but keeps ctx set (does not wedge init guard)', async () => {
    const { ctx } = makeMockContext();
    let degraded = false;
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      crossOriginSafe: true,
      onSuspendedFail: () => { degraded = true; },
    });
    expect(eq.isRerouted).toBe(true);

    ctx.state = 'suspended';
    ctx.resume = vi.fn(async () => {
      throw new Error('NotAllowedError: no user gesture');
    });

    await eq.resume();

    expect(degraded, 'onSuspendedFail must fire so the UI can degrade').toBe(true);
    expect((eq as unknown as { ctx: unknown }).ctx, 'ctx must stay set so init guard holds').toBe(ctx);
    expect(eq.isRerouted, 'graph must stay routed (tearing it down would silence the still-bound element)').toBe(true);
  });
});

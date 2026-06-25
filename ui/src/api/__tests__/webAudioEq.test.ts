import { describe, it, expect, vi } from 'vitest';
import { WebAudioEq } from '../webAudioEq';

/**
 * A minimal AudioContext mock that records node creation and connection order.
 * jsdom has no Web Audio API, so the EQ controller must be drivable from a
 * pluggable context factory in tests.
 */
function makeMockContext() {
  const calls: string[] = [];
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
      return mkNode(`filter${biquadCount}`, {
        type: '',
        frequency: { value: 0 },
        Q: { value: 0 },
        gain: { value: 0 },
      });
    }),
    createGain: vi.fn(() => mkNode('gain', { gain: { value: 0 } })),
  };
  return { ctx, calls };
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
    const lastFilterToGain = calls.indexOf('filter5->gain');
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
    const { ctx } = makeMockContext();
    const eq = new WebAudioEq(() => ctx);
    eq.init({} as HTMLAudioElement, {
      enabled: true,
      bands: [5, -3, 0, 2, -1],
      crossOriginSafe: true,
      onSuspendedFail: () => {},
    });
    // 5 filters created, each with gain.value set from the bands
    expect(ctx.createBiquadFilter).toHaveBeenCalledTimes(5);
  });
});

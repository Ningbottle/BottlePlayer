import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { KeepAlive, defineComponent, h, nextTick, ref } from 'vue';
import { mount } from '@vue/test-utils';
import AuroraAtmosphere from '../AuroraAtmosphere.vue';

const isReducedMotionMock = vi.hoisted(() => vi.fn(() => false));

vi.mock('../../../api/motion', () => ({
  isReducedMotion: () => isReducedMotionMock(),
}));

type FakeCtx = {
  setTransform: ReturnType<typeof vi.fn>;
  clearRect: ReturnType<typeof vi.fn>;
  createRadialGradient: ReturnType<typeof vi.fn>;
  fillRect: ReturnType<typeof vi.fn>;
  beginPath: ReturnType<typeof vi.fn>;
  arc: ReturnType<typeof vi.fn>;
  fill: ReturnType<typeof vi.fn>;
  fillStyle: string | CanvasGradient;
  globalAlpha: number;
};

function installCanvasMock(): FakeCtx {
  const gradient = {
    addColorStop: vi.fn(),
  };
  const ctx: FakeCtx = {
    setTransform: vi.fn(),
    clearRect: vi.fn(),
    createRadialGradient: vi.fn(() => gradient),
    fillRect: vi.fn(),
    beginPath: vi.fn(),
    arc: vi.fn(),
    fill: vi.fn(),
    fillStyle: '',
    globalAlpha: 1,
  };
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(ctx as unknown as CanvasRenderingContext2D);
  vi.spyOn(HTMLCanvasElement.prototype, 'getBoundingClientRect').mockReturnValue({
    width: 400,
    height: 300,
    top: 0,
    left: 0,
    right: 400,
    bottom: 300,
    x: 0,
    y: 0,
    toJSON: () => ({}),
  });
  return ctx;
}

describe('AuroraAtmosphere', () => {
  let rafQueue: FrameRequestCallback[];
  let rafIdSeq: number;
  let cancelSpy: ReturnType<typeof vi.fn>;
  let requestSpy: ReturnType<typeof vi.fn>;
  let observers: Array<{
    callback: ResizeObserverCallback;
    observe: ReturnType<typeof vi.fn>;
    disconnect: ReturnType<typeof vi.fn>;
    unobserve: ReturnType<typeof vi.fn>;
  }>;
  let visibilityState: DocumentVisibilityState;
  /** Track mounts so afterEach always detaches listeners/observers. */
  const openWrappers: Array<{ unmount: () => void }> = [];

  beforeEach(() => {
    isReducedMotionMock.mockReset();
    isReducedMotionMock.mockReturnValue(false);
    rafQueue = [];
    rafIdSeq = 1;
    observers = [];
    openWrappers.length = 0;

    requestSpy = vi.fn((cb: FrameRequestCallback) => {
      const id = rafIdSeq++;
      rafQueue.push(cb);
      return id;
    });
    cancelSpy = vi.fn((id: number) => {
      void id;
      // Drop pending callbacks so a cancelled loop does not continue when flushed.
      rafQueue = [];
    });
    vi.stubGlobal('requestAnimationFrame', requestSpy);
    vi.stubGlobal('cancelAnimationFrame', cancelSpy);

    class MockResizeObserver {
      callback: ResizeObserverCallback;
      observe = vi.fn();
      disconnect = vi.fn();
      unobserve = vi.fn();
      constructor(callback: ResizeObserverCallback) {
        this.callback = callback;
        observers.push(this);
      }
    }
    vi.stubGlobal('ResizeObserver', MockResizeObserver);

    visibilityState = 'visible';
    Object.defineProperty(document, 'visibilityState', {
      configurable: true,
      get: () => visibilityState,
    });

    installCanvasMock();
  });

  afterEach(() => {
    while (openWrappers.length) {
      try {
        openWrappers.pop()?.unmount();
      } catch {
        // already unmounted
      }
    }
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  function track<T extends { unmount: () => void }>(wrapper: T): T {
    openWrappers.push(wrapper);
    return wrapper;
  }

  function flushFrames(n = 1): void {
    for (let i = 0; i < n; i++) {
      const batch = rafQueue.splice(0, rafQueue.length);
      if (batch.length === 0) break;
      for (const cb of batch) cb(performance.now());
    }
  }

  function setVisibility(state: DocumentVisibilityState): void {
    visibilityState = state;
    document.dispatchEvent(new Event('visibilitychange'));
  }

  it('mounts a dedicated canvas behind stage content', () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    const canvas = wrapper.get('[data-test="aurora-atmosphere"]');
    expect(canvas.element.tagName).toBe('CANVAS');
    expect(canvas.attributes('aria-hidden')).toBe('true');
    expect(canvas.classes()).toContain('aurora-atmosphere');
    expect(Number(canvas.attributes('data-particle-cap'))).toBe(100);
  });

  it('uses a calmer particle cap while playing', async () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    expect(wrapper.get('[data-test="aurora-atmosphere"]').attributes('data-particle-cap')).toBe('100');

    await wrapper.setProps({ isPlaying: true });
    expect(wrapper.get('[data-test="aurora-atmosphere"]').attributes('data-particle-cap')).toBe('140');
  });

  it('schedules a single rAF loop when motion is allowed', () => {
    track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    expect(requestSpy).toHaveBeenCalledTimes(1);
    flushFrames(1);
    // Loop reschedules exactly one next frame (no concurrent loops).
    expect(requestSpy).toHaveBeenCalledTimes(2);
    expect(rafQueue).toHaveLength(1);
  });

  it('does not schedule rAF under reduced motion (static wash only)', async () => {
    isReducedMotionMock.mockReturnValue(true);
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    await nextTick();
    expect(requestSpy).not.toHaveBeenCalled();
    expect(wrapper.get('[data-test="aurora-atmosphere"]').attributes('data-loop')).toBe('0');
    expect(wrapper.get('[data-test="aurora-atmosphere"]').attributes('data-motion')).toBe('static');
  });

  it('cancels the scheduled frame on unmount and disconnects observer once', () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    expect(observers).toHaveLength(1);
    expect(requestSpy).toHaveBeenCalledTimes(1);
    const scheduledId = requestSpy.mock.results[0]?.value as number;

    wrapper.unmount();

    expect(cancelSpy).toHaveBeenCalledWith(scheduledId);
    expect(observers[0]?.disconnect).toHaveBeenCalledTimes(1);
    // No further frames after unmount.
    const callsAfterUnmount = requestSpy.mock.calls.length;
    flushFrames(3);
    expect(requestSpy.mock.calls.length).toBe(callsAfterUnmount);
  });

  it('stops the loop on KeepAlive deactivate and starts one loop on reactivate', async () => {
    const current = ref<'atm' | 'other'>('atm');
    const Host = defineComponent({
      setup() {
        return () =>
          h(KeepAlive, null, {
            default: () =>
              current.value === 'atm'
                ? h(AuroraAtmosphere, { key: 'atm', isPlaying: true })
                : h('div', { key: 'other', 'data-test': 'other' }, 'other'),
          });
      },
    });

    const wrapper = track(mount(Host));
    await nextTick();
    expect(requestSpy).toHaveBeenCalledTimes(1);
    const firstId = requestSpy.mock.results[0]?.value as number;

    current.value = 'other';
    await nextTick();
    expect(cancelSpy).toHaveBeenCalledWith(firstId);

    const callsBeforeReactivate = requestSpy.mock.calls.length;
    current.value = 'atm';
    await nextTick();

    // Exactly one new loop (not stacked concurrent loops).
    expect(requestSpy.mock.calls.length - callsBeforeReactivate).toBe(1);
    flushFrames(1);
    expect(rafQueue).toHaveLength(1);

    wrapper.unmount();
  });

  it('stops on visibility hidden and resumes once when visible again', () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    expect(requestSpy).toHaveBeenCalledTimes(1);
    const firstId = requestSpy.mock.results[0]?.value as number;

    setVisibility('hidden');
    expect(cancelSpy).toHaveBeenCalledWith(firstId);
    const callsWhileHidden = requestSpy.mock.calls.length;
    flushFrames(2);
    expect(requestSpy.mock.calls.length).toBe(callsWhileHidden);

    setVisibility('visible');
    expect(requestSpy.mock.calls.length - callsWhileHidden).toBe(1);
    flushFrames(1);
    expect(rafQueue).toHaveLength(1);

    wrapper.unmount();
  });

  it('removes visibilitychange listener on unmount (cleanup once)', () => {
    const addSpy = vi.spyOn(document, 'addEventListener');
    const removeSpy = vi.spyOn(document, 'removeEventListener');

    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    const visibilityAdds = addSpy.mock.calls.filter((c) => c[0] === 'visibilitychange');
    expect(visibilityAdds).toHaveLength(1);

    wrapper.unmount();
    const visibilityRemoves = removeSpy.mock.calls.filter((c) => c[0] === 'visibilitychange');
    expect(visibilityRemoves).toHaveLength(1);
    expect(visibilityRemoves[0]?.[1]).toBe(visibilityAdds[0]?.[1]);
  });

  it('does not create multiple concurrent loops when isPlaying toggles', async () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    expect(requestSpy).toHaveBeenCalledTimes(1);

    await wrapper.setProps({ isPlaying: true });
    await wrapper.setProps({ isPlaying: false });
    await wrapper.setProps({ isPlaying: true });

    // Still a single in-flight callback queue (startLoop is idempotent while running).
    expect(rafQueue.length).toBeLessThanOrEqual(1);
    flushFrames(1);
    expect(rafQueue).toHaveLength(1);

    wrapper.unmount();
  });

  it('requests only Canvas 2D context (no WebGL)', () => {
    const getContextSpy = vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockImplementation(() => {
      return {
        setTransform: vi.fn(),
        clearRect: vi.fn(),
        createRadialGradient: vi.fn(() => ({ addColorStop: vi.fn() })),
        fillRect: vi.fn(),
        beginPath: vi.fn(),
        arc: vi.fn(),
        fill: vi.fn(),
        fillStyle: '',
        globalAlpha: 1,
      } as unknown as CanvasRenderingContext2D;
    });
    track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    expect(getContextSpy).toHaveBeenCalled();
    expect(getContextSpy.mock.calls.every((c) => c[0] === '2d')).toBe(true);
  });
});

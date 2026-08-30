import { describe, it, expect, vi, beforeEach, afterEach, type Mock } from 'vitest';
import { KeepAlive, defineComponent, h, nextTick, ref } from 'vue';
import { mount } from '@vue/test-utils';
import AuroraAtmosphere from '../AuroraAtmosphere.vue';

const isReducedMotionMock = vi.hoisted(() => vi.fn(() => false));

vi.mock('../../../api/motion', () => ({
  isReducedMotion: () => isReducedMotionMock(),
}));

type FakeCtx = {
  setTransform: Mock;
  clearRect: Mock;
  createRadialGradient: Mock;
  createLinearGradient: Mock;
  fillRect: Mock;
  beginPath: Mock;
  arc: Mock;
  fill: Mock;
  save: Mock;
  restore: Mock;
  translate: Mock;
  rotate: Mock;
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
    createLinearGradient: vi.fn(() => gradient),
    fillRect: vi.fn(),
    beginPath: vi.fn(),
    arc: vi.fn(),
    fill: vi.fn(),
    save: vi.fn(),
    restore: vi.fn(),
    translate: vi.fn(),
    rotate: vi.fn(),
    fillStyle: '',
    globalAlpha: 1,
  };
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(ctx as unknown as CanvasRenderingContext2D);
  rectSpy = vi.spyOn(HTMLCanvasElement.prototype, 'getBoundingClientRect').mockReturnValue({
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

/** The prototype-level getBoundingClientRect spy installed by installCanvasMock. */
let rectSpy: Mock = vi.fn();
let devicePixelRatioValue = 1;

function setDpr(dpr: number): void {
  devicePixelRatioValue = dpr;
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
    rectSpy = vi.fn();
    devicePixelRatioValue = 1;

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

    Object.defineProperty(window, 'devicePixelRatio', {
      configurable: true,
      get: () => devicePixelRatioValue,
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

  /** Fire the first live ResizeObserver with a minimal ResizeObserverEntry. */
  function emitResize(width: number, height: number): void {
    const observer = observers[0];
    expect(observer).toBeTruthy();
    const target = document.createElement('canvas');
    const entry = { target, contentRect: { width, height } } as unknown as ResizeObserverEntry;
    observer.callback([entry], observer as unknown as ResizeObserver);
  }

  function rectCallCount(): number {
    return rectSpy.mock.calls.length;
  }

  it('mounts a dedicated canvas behind stage content', () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    const canvas = wrapper.get('[data-test="aurora-atmosphere"]');
    expect(canvas.element.tagName).toBe('CANVAS');
    expect(canvas.attributes('aria-hidden')).toBe('true');
    expect(canvas.classes()).toContain('aurora-atmosphere');
    expect(canvas.attributes('data-playing')).toBe('false');
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
        createLinearGradient: vi.fn(() => ({ addColorStop: vi.fn() })),
        fillRect: vi.fn(),
        beginPath: vi.fn(),
        arc: vi.fn(),
        fill: vi.fn(),
        save: vi.fn(),
        restore: vi.fn(),
        translate: vi.fn(),
        rotate: vi.fn(),
        fillStyle: '',
        globalAlpha: 1,
      } as unknown as CanvasRenderingContext2D;
    });
    track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    expect(getContextSpy).toHaveBeenCalled();
    expect(getContextSpy.mock.calls.every((c) => c[0] === '2d')).toBe(true);
  });

  // ── B5: rAF must never read layout; ResizeObserver owns size via contentRect ──

  it('B5: active rAF frames do not add getBoundingClientRect calls after mount', () => {
    track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    // Mount does exactly one initial layout measurement.
    const callsAfterMount = rectCallCount();
    expect(callsAfterMount).toBe(1);

    flushFrames(2);

    // Running animation frames must not read layout.
    expect(rectCallCount()).toBe(callsAfterMount);
  });

  it('B5: reduced-motion resize applies contentRect backing size before the early return', () => {
    isReducedMotionMock.mockReturnValue(true);
    setDpr(2);
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: true } }));
    const canvas = wrapper.get('[data-test="aurora-atmosphere"]').element as HTMLCanvasElement;
    expect(requestSpy).not.toHaveBeenCalled();

    const callsBefore = rectCallCount();
    emitResize(250, 120);

    // Backing store updated from the observer entry, without layout reads.
    expect(canvas.width).toBe(500);
    expect(canvas.height).toBe(240);
    expect(rectCallCount()).toBe(callsBefore);
    // Reduced-motion branch stays static: no rAF loop started.
    expect(requestSpy).not.toHaveBeenCalled();
  });

  it('B5: hidden resize updates cached size without restarting the loop or reading layout', () => {
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    const callsAfterMount = rectCallCount();
    const scheduledId = requestSpy.mock.results[0]?.value as number;

    setVisibility('hidden');
    expect(cancelSpy).toHaveBeenCalledWith(scheduledId);
    const callsWhileHidden = requestSpy.mock.calls.length;

    emitResize(320, 200);
    const canvas = wrapper.get('[data-test="aurora-atmosphere"]').element as HTMLCanvasElement;

    expect(canvas.width).toBe(320);
    expect(canvas.height).toBe(200);
    // No new rAF and no layout read while hidden.
    expect(requestSpy.mock.calls.length).toBe(callsWhileHidden);
    expect(rectCallCount()).toBe(callsAfterMount);

    wrapper.unmount();
  });

  it('B5: KeepAlive inactive resize updates cached size without starting a loop; repaint uses it after reactivation', async () => {
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
    const callsAfterMount = rectCallCount();
    // Hold the element reference before deactivation: KeepAlive removes the
    // component from the rendered tree, so findComponent cannot reach it.
    const canvas = (wrapper.findComponent(AuroraAtmosphere).element as HTMLCanvasElement);

    current.value = 'other';
    await nextTick();

    emitResize(600, 240);
    expect(canvas.width).toBe(600);
    expect(canvas.height).toBe(240);
    // Deactivated tree must not start a loop or read layout.
    expect(requestSpy.mock.calls.length).toBeLessThanOrEqual(1);
    expect(rectCallCount()).toBe(callsAfterMount);

    // Reactivation repaints with the updated cached size (no extra rect read).
    const callsBeforeReactivate = rectCallCount();
    current.value = 'atm';
    await nextTick();
    expect(rectCallCount()).toBe(callsBeforeReactivate);
    flushFrames(1);
    expect(rectCallCount()).toBe(callsBeforeReactivate);

    wrapper.unmount();
  });

  it('B5: DPR-only change resizes the backing store from cached CSS size without layout reads', () => {
    setDpr(1);
    const wrapper = track(mount(AuroraAtmosphere, { props: { isPlaying: false } }));
    const canvas = wrapper.get('[data-test="aurora-atmosphere"]').element as HTMLCanvasElement;
    expect(canvas.width).toBe(400);
    expect(canvas.height).toBe(300);
    const callsAfterMount = rectCallCount();
    expect(callsAfterMount).toBe(1);

    // DPR changes without a ResizeObserver event.
    setDpr(2);
    flushFrames(2);

    expect(canvas.width).toBe(800);
    expect(canvas.height).toBe(600);
    expect(rectCallCount()).toBe(callsAfterMount);
  });
});

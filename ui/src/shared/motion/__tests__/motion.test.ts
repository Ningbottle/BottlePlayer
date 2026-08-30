import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ref } from 'vue';

vi.mock('gsap', () => {
  const to = vi.fn((target, opts) => {
    // Simulate count-up by calling onUpdate with the target value immediately.
    if (opts.onUpdate) {
      const obj = typeof target === 'object' ? target : { value: target };
      obj.value = opts.value;
      opts.onUpdate();
    }
    // Simulate the tween completing (real gsap calls onComplete when finished).
    if (opts.onComplete) opts.onComplete();
    return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
  });
  const fromTo = vi.fn((_, __, opts) => {
    if (opts.onComplete) opts.onComplete();
    return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
  });
  const set = vi.fn();
  const timeline = vi.fn(() => {
    const tl: any = { to: vi.fn((_, o) => { if (o?.onComplete) o.onComplete(); return tl; }), kill: vi.fn() };
    return tl;
  });
  const matchMedia = vi.fn(() => ({ add: vi.fn((_, cb) => cb()), revert: vi.fn() }));
  const killTweensOf = vi.fn();
  return { gsap: { to, fromTo, set, timeline, matchMedia, killTweensOf } };
});

import {
  animateBarHeight,
  animateCountUp,
  crossfadeTheme,
  isReducedMotion,
  animateElement,
  animateStagger,
  startVinylSpin,
  pressBounceDown,
  pressBounceUp,
  configureMotionProfileProvider,
  resetMotionProfileProviderForTests,
} from '../motion';

describe('motionPrimitives', () => {
  beforeEach(async () => {
    vi.clearAllMocks();
    vi.unstubAllGlobals();
    resetMotionProfileProviderForTests();
    // Restore default gsap mock behavior after tests that replace implementations
    const { gsap } = await import('gsap');
    (gsap.to as ReturnType<typeof vi.fn>).mockImplementation((target, opts) => {
      if (opts.onUpdate) {
        const obj = typeof target === 'object' ? target : { value: target };
        obj.value = opts.value;
        opts.onUpdate();
      }
      if (opts.onComplete) opts.onComplete();
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
    (gsap.fromTo as ReturnType<typeof vi.fn>).mockImplementation((_, __, opts) => {
      if (opts.onComplete) opts.onComplete();
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
  });

  afterEach(() => {
    Object.defineProperty(document, 'hidden', { value: false, configurable: true, writable: true });
  });

  it('animateCountUp calls gsap.to with the target value and onUpdate', async () => {
    const target = ref(0);
    await animateCountUp(target, 42, { duration: 0.1 });
    expect(target.value).toBe(42);
  });

  it('animateCountUp defaults to expo.out for a crisper count', async () => {
    const { gsap } = await import('gsap');
    const target = ref(0);

    await animateCountUp(target, 42);

    expect(gsap.to).toHaveBeenLastCalledWith(
      expect.any(Object),
      expect.objectContaining({ value: 42, ease: 'expo.out' }),
    );
  });

  it('animateBarHeight defaults to expo.out and target height', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    animateBarHeight(el, 76);

    expect(gsap.to).toHaveBeenLastCalledWith(
      el,
      expect.objectContaining({ height: 76, ease: 'expo.out' }),
    );
    expect(gsap.killTweensOf).toHaveBeenCalledWith(el);
  });

  it('crossfadeTheme calls applyFn at the opacity dip', async () => {
    const applyFn = vi.fn();
    await crossfadeTheme(applyFn);
    expect(applyFn).toHaveBeenCalled();
  });

  it('isReducedMotion returns false when matchMedia does not reduce', () => {
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: false })));
    expect(isReducedMotion()).toBe(false);
  });

  it('isReducedMotion returns true when prefers-reduced-motion: reduce', () => {
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    expect(isReducedMotion()).toBe(true);
  });

  // --- Neutral profile provider (replaces themeStore reads) ---

  it('animateElement resolves controlRelease through the injected provider', async () => {
    const { gsap } = await import('gsap');
    configureMotionProfileProvider(() => 'aurora');
    const el = document.createElement('div');

    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlRelease');

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0 },
      expect.objectContaining({ ease: 'elastic.out(1.12, 0.42)' }),
    );
  });

  it('animateElement switches profiles live when the provider returns newsprint', async () => {
    const { gsap } = await import('gsap');
    let skin: 'aurora' | 'newsprint' = 'aurora';
    configureMotionProfileProvider(() => skin);
    const el = document.createElement('div');

    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlPress');
    expect(gsap.fromTo).toHaveBeenLastCalledWith(
      el,
      { opacity: 0 },
      expect.objectContaining({ duration: 0.08 }),
    );

    skin = 'newsprint';
    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlPress');
    expect(gsap.fromTo).toHaveBeenLastCalledWith(
      el,
      { opacity: 0 },
      expect.objectContaining({ duration: 0.1 }),
    );
  });

  it('defaults to the application default skin (aurora) without themeStore or localStorage', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('button');

    pressBounceDown(el);

    // Aurora controlPress duration with no provider configured at all.
    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ scale: 0.86, duration: 0.08 }),
    );
  });

  it('animateElement in reduced motion sets final state without gsap.to', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const el = document.createElement('div');

    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlPress');

    expect(gsap.set).toHaveBeenCalledWith(el, { opacity: 1 });
    expect(gsap.to).not.toHaveBeenCalled();
    expect(gsap.fromTo).not.toHaveBeenCalled();
  });

  // --- animateElement tests (generic) ---

  it('animateElement calls gsap.killTweensOf before starting', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlPress');

    expect(gsap.killTweensOf).toHaveBeenCalledWith(el);
  });

  it('animateElement returns handle with kill()', () => {
    const el = document.createElement('div');
    const handle = animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlPress');
    expect(typeof handle.kill).toBe('function');
  });

  // --- animateStagger tests ---

  it('animateStagger calls gsap.killTweensOf for each element', async () => {
    const { gsap } = await import('gsap');
    const els = [document.createElement('div'), document.createElement('div')];

    animateStagger(els, 'cardEnter');

    expect(gsap.killTweensOf).toHaveBeenCalledTimes(2);
  });

  it('animateStagger returns handle with kill()', () => {
    const els = [document.createElement('div')];
    const handle = animateStagger(els, 'cardEnter');
    expect(typeof handle.kill).toBe('function');
  });

  it('animateStagger uses cardEnter profile with stagger', async () => {
    const { gsap } = await import('gsap');
    const els = [document.createElement('div'), document.createElement('div')];

    animateStagger(els, 'cardEnter');

    expect(gsap.fromTo).toHaveBeenCalledWith(
      els,
      { opacity: 0, y: 20 },
      expect.objectContaining({ stagger: 0.04 }),
    );
  });

  it('animateStagger in reduced motion sets final state without gsap.fromTo', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const els = [document.createElement('div'), document.createElement('div')];

    animateStagger(els, 'cardEnter');

    expect(gsap.set).toHaveBeenCalledWith(els, { opacity: 1, y: 0 });
    expect(gsap.fromTo).not.toHaveBeenCalled();
  });

  it('pressBounceDown scales down and pressBounceUp springs with elastic', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('button');
    pressBounceDown(el);
    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ scale: 0.86, duration: 0.08 }),
    );
    pressBounceUp(el);
    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ scale: 1, ease: expect.stringContaining('elastic') }),
    );
  });

  it('pressBounceUp uses the interruptible Aurora controlRelease profile', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('button');

    pressBounceUp(el);

    expect(gsap.to).toHaveBeenLastCalledWith(
      el,
      expect.objectContaining({ duration: 0.58, ease: 'elastic.out(1.12, 0.42)' }),
    );
  });

  it('animateStagger merges overrides for duration, stagger, maxItems, and fromY', async () => {
    const { gsap } = await import('gsap');
    const els = Array.from({ length: 10 }, () => document.createElement('div'));

    animateStagger(els, 'cardEnter', {
      duration: 0.24,
      stagger: 0.025,
      maxItems: 6,
      fromY: 10,
    });

    expect(gsap.fromTo).toHaveBeenCalledWith(
      els.slice(0, 6),
      { opacity: 0, y: 10 },
      expect.objectContaining({
        duration: 0.24,
        stagger: 0.025,
      }),
    );
    // Only the capped set is killed / animated
    expect(gsap.killTweensOf).toHaveBeenCalledTimes(6);
  });

  it('animateStagger without overrides keeps profile cardEnter defaults', async () => {
    const { gsap } = await import('gsap');
    const els = Array.from({ length: 15 }, () => document.createElement('div'));

    animateStagger(els, 'cardEnter');

    expect(gsap.fromTo).toHaveBeenCalledWith(
      els.slice(0, 12),
      { opacity: 0, y: 20 },
      expect.objectContaining({
        duration: 0.4,
        stagger: 0.04,
      }),
    );
  });

  // --- startVinylSpin tests ---

  it('startVinylSpin creates a paused infinite spin from the aurora vinyl profile', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    const handle = startVinylSpin(el, () => false);

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({
        rotation: '+=360',
        duration: 24,
        ease: 'none',
        repeat: -1,
        paused: true,
      }),
    );

    handle.kill();
  });

  it('startVinylSpin ramps timeScale with playback state', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    let playing = false;

    const handle = startVinylSpin(el, () => playing);
    // Initial sync: not playing → ramp the deck down to 0
    expect(gsap.to).toHaveBeenLastCalledWith(
      expect.anything(),
      expect.objectContaining({ timeScale: 0, duration: 0.8 }),
    );

    playing = true;
    handle.setPlaying();
    expect(gsap.to).toHaveBeenLastCalledWith(
      expect.anything(),
      expect.objectContaining({ timeScale: 1, duration: 0.8 }),
    );

    handle.kill();
  });

  it('startVinylSpin does not spin for newsprint (provider-driven)', async () => {
    const { gsap } = await import('gsap');
    configureMotionProfileProvider(() => 'newsprint');
    const el = document.createElement('div');

    const handle = startVinylSpin(el, () => true);

    expect(gsap.to).not.toHaveBeenCalled();
    handle.kill();
  });

  it('startVinylSpin is inert in reduced motion', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const el = document.createElement('div');

    const handle = startVinylSpin(el, () => true);

    expect(gsap.to).not.toHaveBeenCalled();
    handle.kill();
  });

  it('startVinylSpin burst scratches up to 3x and eases back while playing', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    const handle = startVinylSpin(el, () => true);
    (gsap.to as ReturnType<typeof vi.fn>).mockClear();

    handle.burst();
    // Mock fires onComplete immediately, so both ramp legs are observable.
    expect(gsap.to).toHaveBeenCalledWith(expect.anything(), expect.objectContaining({ timeScale: 3, duration: 0.18 }));
    expect(gsap.to).toHaveBeenLastCalledWith(expect.anything(), expect.objectContaining({ timeScale: 1, duration: 0.55 }));

    handle.kill();
  });

  it('startVinylSpin burst is a no-op while paused', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    const handle = startVinylSpin(el, () => false);
    (gsap.to as ReturnType<typeof vi.fn>).mockClear();

    handle.burst();
    expect(gsap.to).not.toHaveBeenCalled();

    handle.kill();
  });

  it('startVinylSpin kill prevents future ramps', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const playing = true;

    const handle = startVinylSpin(el, () => playing);
    handle.kill();

    (gsap.to as ReturnType<typeof vi.fn>).mockClear();
    handle.setPlaying();
    Object.defineProperty(document, 'hidden', { value: false, configurable: true, writable: true });
    document.dispatchEvent(new Event('visibilitychange'));

    expect(gsap.to).not.toHaveBeenCalled();
  });
});

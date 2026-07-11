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
    return { kill: vi.fn() };
  });
  const fromTo = vi.fn((_, __, opts) => {
    if (opts.onComplete) opts.onComplete();
    return { kill: vi.fn() };
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
  transitionEnter,
  transitionLeave,
  animateElement,
  animateStagger,
  startAmbientMotion,
} from '../motion';
import { useThemeStore, __resetForTest } from '../themeStore';

describe('motion.ts', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.unstubAllGlobals();
    __resetForTest();
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

  it('transitionEnter uses stronger distance and expo.out', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0, y: 20 },
      expect.objectContaining({ opacity: 1, y: 0, ease: 'expo.out' }),
    );
  });

  it('transitionLeave stays fast and calls gsap.to', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionLeave(el, done);

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ opacity: 0, y: -16, duration: 0.18 }),
    );
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

  // --- Profile-based transition tests ---

  it('transitionEnter uses newsprint pageEnter ease when skin is newsprint', async () => {
    const { gsap } = await import('gsap');
    const { skinId } = useThemeStore();
    skinId.value = 'newsprint';
    const el = document.createElement('div');

    transitionEnter(el, vi.fn());

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0, y: 20 },
      expect.objectContaining({ ease: 'power3.out' }),
    );
  });

  it('transitionLeave uses newsprint pageLeave duration when skin is newsprint', async () => {
    const { gsap } = await import('gsap');
    const { skinId } = useThemeStore();
    skinId.value = 'newsprint';
    const el = document.createElement('div');

    transitionLeave(el, vi.fn());

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ duration: 0.16 }),
    );
  });

  // --- animateElement tests ---

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

  it('animateElement uses aurora controlRelease elastic.out ease', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');

    animateElement(el, { opacity: 0 }, { opacity: 1 }, 'controlRelease');

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0 },
      expect.objectContaining({ ease: 'elastic.out(1, 0.5)' }),
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

  // --- startAmbientMotion tests ---

  it('startAmbientMotion returns handle with kill()', () => {
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);
    expect(typeof handle.kill).toBe('function');
    handle.kill();
  });

  it('startAmbientMotion starts for Aurora when playing', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);

    expect(gsap.to).toHaveBeenCalled();

    handle.kill();
  });

  it('startAmbientMotion does not start for newsprint', async () => {
    const { gsap } = await import('gsap');
    const { skinId } = useThemeStore();
    skinId.value = 'newsprint';
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);

    expect(gsap.to).not.toHaveBeenCalled();

    handle.kill();
  });

  it('startAmbientMotion does not start when not playing', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => false);

    expect(gsap.to).not.toHaveBeenCalled();

    handle.kill();
  });

  it('startAmbientMotion does not start in reduced motion', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);

    expect(gsap.to).not.toHaveBeenCalled();

    handle.kill();
  });

  it('startAmbientMotion pauses on document.hidden', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);

    // After start, killTweensOf called once (in start)
    expect(gsap.killTweensOf).toHaveBeenCalledTimes(1);

    // Simulate document becoming hidden
    Object.defineProperty(document, 'hidden', { value: true, configurable: true, writable: true });
    document.dispatchEvent(new Event('visibilitychange'));

    // pause() should call killTweensOf again
    expect(gsap.killTweensOf).toHaveBeenCalledTimes(2);

    handle.kill();
  });

  it('startAmbientMotion kill prevents future resume', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const handle = startAmbientMotion(el, () => true);

    handle.kill();

    // Clear mock and dispatch visibility change - should not start new tween
    (gsap.to as ReturnType<typeof vi.fn>).mockClear();
    Object.defineProperty(document, 'hidden', { value: false, configurable: true, writable: true });
    document.dispatchEvent(new Event('visibilitychange'));

    expect(gsap.to).not.toHaveBeenCalled();
  });
});

import { describe, it, expect, vi, beforeEach } from 'vitest';
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
  const timeline = vi.fn(() => {
    const tl: any = { to: vi.fn((_, o) => { if (o?.onComplete) o.onComplete(); return tl; }), kill: vi.fn() };
    return tl;
  });
  const matchMedia = vi.fn(() => ({ add: vi.fn((_, cb) => cb()), revert: vi.fn() }));
  const killTweensOf = vi.fn();
  return { gsap: { to, fromTo, timeline, matchMedia, killTweensOf } };
});

import { animateBarHeight, animateCountUp, crossfadeTheme, isReducedMotion, transitionEnter, transitionLeave } from '../motion';

describe('motion.ts', () => {
  beforeEach(() => {
    vi.clearAllMocks();
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
});

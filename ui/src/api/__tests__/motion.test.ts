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
  const timeline = vi.fn(() => {
    const tl: any = { to: vi.fn((_, o) => { if (o?.onComplete) o.onComplete(); return tl; }), kill: vi.fn() };
    return tl;
  });
  const matchMedia = vi.fn(() => ({ add: vi.fn((_, cb) => cb()), revert: vi.fn() }));
  return { gsap: { to, timeline, matchMedia } };
});

import { animateCountUp, crossfadeTheme, isReducedMotion } from '../motion';

describe('motion.ts', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('animateCountUp calls gsap.to with the target value and onUpdate', async () => {
    const target = ref(0);
    await animateCountUp(target, 42, { duration: 0.1 });
    expect(target.value).toBe(42);
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

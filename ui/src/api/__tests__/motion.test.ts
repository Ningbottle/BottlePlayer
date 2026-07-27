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
  transitionEnter,
  transitionLeave,
  animateElement,
  animateStagger,
  startVinylSpin,
  pressBounceDown,
  pressBounceUp,
} from '../motion';
import { useThemeStore, __resetForTest } from '../themeStore';

describe('motion.ts', () => {
  beforeEach(async () => {
    vi.clearAllMocks();
    vi.unstubAllGlobals();
    __resetForTest();
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

  it('transitionEnter uses the Aurora pageEnter profile', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0, y: 28 },
      expect.objectContaining({ opacity: 1, y: 0, ease: 'expo.out', duration: 0.56 }),
    );
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionLeave stays fast and calls gsap.to', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionLeave(el, done);

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ opacity: 0, y: -16, duration: 0.2 }),
    );
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionEnter kill/onInterrupt still settles done exactly once', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();
    let interrupt: (() => void) | undefined;

    (gsap.fromTo as ReturnType<typeof vi.fn>).mockImplementationOnce((_el, _from, opts) => {
      interrupt = opts.onInterrupt;
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });

    transitionEnter(el, done);
    expect(done).not.toHaveBeenCalled();
    expect(gsap.killTweensOf).toHaveBeenCalledWith(el);

    interrupt?.();
    interrupt?.();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionLeave kill/onInterrupt still settles done exactly once', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();
    let interrupt: (() => void) | undefined;

    (gsap.to as ReturnType<typeof vi.fn>).mockImplementationOnce((_el, opts) => {
      interrupt = opts.onInterrupt;
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });

    transitionLeave(el, done);
    expect(done).not.toHaveBeenCalled();
    expect(gsap.killTweensOf).toHaveBeenCalledWith(el);

    interrupt?.();
    interrupt?.();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('rapid re-enter on same el interrupts prior session; each done once', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done1 = vi.fn();
    const done2 = vi.fn();
    let secondComplete: (() => void) | undefined;

    (gsap.fromTo as ReturnType<typeof vi.fn>)
      .mockImplementationOnce(() => ({ kill: vi.fn() }))
      .mockImplementationOnce((_el, _from, opts) => {
        secondComplete = opts.onComplete;
        return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
      });

    transitionEnter(el, done1);
    transitionEnter(el, done2);

    // beginTransitionSession on same el settles the first session immediately
    expect(done1).toHaveBeenCalledTimes(1);
    expect(done2).not.toHaveBeenCalled();

    secondComplete?.();
    secondComplete?.();
    expect(done2).toHaveBeenCalledTimes(1);
    expect(done1).toHaveBeenCalledTimes(1);
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
      { opacity: 0, y: 8 },
      expect.objectContaining({ ease: 'power3.out', duration: 0.24 }),
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

  it('transitionEnter reduced motion completes session without fromTo', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).not.toHaveBeenCalled();
    expect(gsap.set).toHaveBeenCalled();
    expect(done).toHaveBeenCalledTimes(1);
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
      expect.objectContaining({ ease: 'elastic.out(1.12, 0.42)' }),
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

  it('startVinylSpin does not spin for newsprint', async () => {
    const { gsap } = await import('gsap');
    const { skinId } = useThemeStore();
    skinId.value = 'newsprint';
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

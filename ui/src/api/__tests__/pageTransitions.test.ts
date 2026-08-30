import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('gsap', () => {
  const to = vi.fn((_, opts) => {
    if (opts?.onComplete) opts.onComplete();
    return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
  });
  const fromTo = vi.fn((_, __, opts) => {
    if (opts?.onComplete) opts.onComplete();
    return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
  });
  const set = vi.fn();
  const killTweensOf = vi.fn();
  return { gsap: { to, fromTo, set, killTweensOf } };
});

vi.mock('../../app/appearance/themeStore', () => {
  const skinId = { value: 'aurora' };
  const useThemeStore = () => ({ skinId });
  return { useThemeStore };
});

// Direction lives in navigation; the adapter reads it per transition.
vi.mock('../../navigation/direction', () => {
  const navigationDirection = { value: 'forward' as 'forward' | 'back' };
  return { navigationDirection };
});

vi.mock('../transitionSession', () => {
  const sessions = new Map<Element, { settle: (r: 'complete' | 'interrupt') => void }>();
  function beginTransitionSession(el: Element, phase: 'enter' | 'leave', done?: () => void) {
    sessions.get(el)?.settle('interrupt');
    // Snapshot BEFORE the adapter mutates inline styles — the real module does
    // exactly this, and restores on interrupt and on completed leaves.
    const snapshot = {
      opacity: (el as HTMLElement).style.opacity,
      transform: (el as HTMLElement).style.transform,
      filter: (el as HTMLElement).style.filter,
      pointerEvents: (el as HTMLElement).style.pointerEvents,
    };
    const restore = () => {
      (el as HTMLElement).style.opacity = snapshot.opacity;
      (el as HTMLElement).style.transform = snapshot.transform;
      (el as HTMLElement).style.filter = snapshot.filter;
      (el as HTMLElement).style.pointerEvents = snapshot.pointerEvents;
    };
    let settled = false;
    const settle = (reason: 'complete' | 'interrupt') => {
      if (settled) return;
      settled = true;
      sessions.delete(el);
      if (reason === 'interrupt' || phase === 'leave') restore();
      done?.();
    };
    const session = { complete: () => settle('complete'), interrupt: () => settle('interrupt') };
    sessions.set(el, { settle });
    return session;
  }
  return { beginTransitionSession };
});

import { transitionEnter, transitionLeave } from '../pageTransitions';
import { navigationDirection } from '../../navigation/direction';
import { useThemeStore } from '../../app/appearance/themeStore';

describe('pageTransitions (navigation adapter)', () => {
  beforeEach(async () => {
    vi.clearAllMocks();
    vi.unstubAllGlobals();
    const { gsap } = await import('gsap');
    (gsap.to as ReturnType<typeof vi.fn>).mockImplementation((_, opts) => {
      if (opts?.onComplete) opts.onComplete();
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
    (gsap.fromTo as ReturnType<typeof vi.fn>).mockImplementation((_, __, opts) => {
      if (opts?.onComplete) opts.onComplete();
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
    const { skinId } = useThemeStore();
    skinId.value = 'aurora';
    navigationDirection.value = 'forward';
  });

  it('transitionEnter uses the Aurora pageEnter profile', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0, x: 24 },
      expect.objectContaining({ opacity: 1, x: 0, ease: 'expo.out', duration: 0.56 }),
    );
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionEnter respects back direction (shared-axis X inverts)', async () => {
    const { gsap } = await import('gsap');
    navigationDirection.value = 'back';
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).toHaveBeenCalledWith(
      el,
      { opacity: 0, x: -24 },
      expect.objectContaining({ opacity: 1, x: 0 }),
    );
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionEnter reduced motion completes immediately without fromTo', async () => {
    const { gsap } = await import('gsap');
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    const el = document.createElement('div');
    const done = vi.fn();

    transitionEnter(el, done);

    expect(gsap.fromTo).not.toHaveBeenCalled();
    expect(gsap.set).toHaveBeenCalled();
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

  it('transitionLeave sets pointer-events none immediately and restores on interrupt', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    el.style.pointerEvents = 'auto';
    let interrupt: (() => void) | undefined;
    (gsap.to as ReturnType<typeof vi.fn>).mockImplementationOnce((_el, opts) => {
      interrupt = opts.onInterrupt;
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
    const done = vi.fn();

    transitionLeave(el, done);
    expect((el as HTMLElement).style.pointerEvents).toBe('none');
    expect(done).not.toHaveBeenCalled();
    interrupt?.();
    expect((el as HTMLElement).style.pointerEvents).toBe('auto');
    interrupt?.();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionLeave restores pointer-events after success so KeepAlive can reuse the page root', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    el.style.pointerEvents = 'auto';
    let complete: (() => void) | undefined;
    (gsap.to as ReturnType<typeof vi.fn>).mockImplementationOnce((_el, opts) => {
      complete = opts.onComplete;
      return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
    });
    const done = vi.fn();

    transitionLeave(el, done);
    expect(el.style.pointerEvents).toBe('none');

    complete?.();

    expect(el.style.pointerEvents).toBe('auto');
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionLeave stays fast (Aurora pageLeave 0.2) and calls gsap.to', async () => {
    const { gsap } = await import('gsap');
    const el = document.createElement('div');
    const done = vi.fn();

    transitionLeave(el, done);

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ opacity: 0, x: -16, duration: 0.2 }),
    );
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('transitionLeave keeps the Newsprint Y transition and duration', async () => {
    const { gsap } = await import('gsap');
    const { skinId } = useThemeStore();
    skinId.value = 'newsprint';
    const el = document.createElement('div');

    transitionLeave(el, vi.fn());

    expect(gsap.to).toHaveBeenCalledWith(
      el,
      expect.objectContaining({ opacity: 0, y: -16, duration: 0.16 }),
    );
  });

  it('transitionEnter uses newsprint pageEnter Y variant when skin is newsprint', async () => {
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
});

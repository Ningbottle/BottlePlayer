import { describe, it, expect, vi } from 'vitest';
import { beginTransitionSession, settleActiveTransitionSessions } from '../transitionSession';
import { cancelPageTransition, registerPageTransition } from '../../navigation/navigationLifecycle';

describe('transitionSession', () => {
  it('does not call done on begin; complete settles exactly once', () => {
    const el = document.createElement('div');
    const done = vi.fn();
    const s = beginTransitionSession(el, 'enter', done);
    expect(done).not.toHaveBeenCalled(); // stub fails here if done() on begin
    s.complete();
    s.complete();
    s.interrupt();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('interrupt settles exactly once when not yet complete', () => {
    const el = document.createElement('div');
    const done = vi.fn();
    const s = beginTransitionSession(el, 'leave', done);
    expect(done).not.toHaveBeenCalled();
    s.interrupt();
    s.interrupt();
    s.complete();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('new session on same el interrupts previous; each done at most once', () => {
    const el = document.createElement('div');
    const done1 = vi.fn();
    const done2 = vi.fn();
    beginTransitionSession(el, 'leave', done1);
    const s2 = beginTransitionSession(el, 'enter', done2);
    expect(done1).toHaveBeenCalledTimes(1);
    expect(done2).not.toHaveBeenCalled();
    s2.complete();
    s2.complete();
    expect(done2).toHaveBeenCalledTimes(1);
    expect(done1).toHaveBeenCalledTimes(1);
  });

  it('allows undefined done without throwing', () => {
    const el = document.createElement('div');
    const s = beginTransitionSession(el, 'enter', undefined);
    expect(() => {
      s.complete();
      s.interrupt();
    }).not.toThrow();
  });

  it('settles all active sessions and restores transition-related inline styles', () => {
    const el = document.createElement('div');
    el.style.opacity = '0.4';
    el.style.transform = 'scale(1)';
    el.style.filter = 'none';
    el.style.pointerEvents = 'auto';
    const done = vi.fn();
    beginTransitionSession(el, 'enter', done);

    el.style.opacity = '0';
    el.style.transform = 'translateY(28px)';
    el.style.filter = 'blur(2px)';
    el.style.pointerEvents = 'none';

    const restore = settleActiveTransitionSessions();

    expect(done).toHaveBeenCalledTimes(1);
    expect(restore).toBeTypeOf('function');
    restore();
    expect(el.style.opacity).toBe('0.4');
    expect(el.style.transform).toBe('scale(1)');
    expect(el.style.filter).toBe('none');
    expect(el.style.pointerEvents).toBe('auto');
    settleActiveTransitionSessions();
    expect(done).toHaveBeenCalledTimes(1);
  });

  it('restores original styles after the exact settle, cancel, and final-restore sequence', () => {
    const el = document.createElement('div');
    el.style.opacity = '0.6';
    el.style.transform = 'scale(1)';
    el.style.filter = 'none';
    el.style.pointerEvents = 'auto';
    registerPageTransition(el);
    beginTransitionSession(el, 'enter', vi.fn());
    el.style.opacity = '0';
    el.style.transform = 'translateY(28px)';
    el.style.filter = 'blur(2px)';
    el.style.pointerEvents = 'none';

    const finalRestore = settleActiveTransitionSessions() as unknown as (() => void);
    cancelPageTransition();
    finalRestore?.();

    expect(el.style.opacity).toBe('0.6');
    expect(el.style.transform).toBe('scale(1)');
    expect(el.style.filter).toBe('none');
    expect(el.style.pointerEvents).toBe('auto');
  });

  it('cancelPageTransition settles Vue transition done even when GSAP does not report interruption', () => {
    const el = document.createElement('div');
    el.style.opacity = '0.8';
    const done = vi.fn();
    registerPageTransition(el);
    beginTransitionSession(el, 'enter', done);
    el.style.opacity = '0';

    try {
      cancelPageTransition();

      expect(done).toHaveBeenCalledTimes(1);
      expect(el.style.opacity).toBe('0.8');
    } finally {
      settleActiveTransitionSessions();
    }
  });
});

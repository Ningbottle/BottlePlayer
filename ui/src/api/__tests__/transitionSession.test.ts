import { describe, it, expect, vi } from 'vitest';
import { beginTransitionSession } from '../transitionSession';

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
});

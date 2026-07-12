export type TransitionPhase = 'enter' | 'leave';

export interface TransitionSession {
  complete(): void;
  interrupt(): void;
}

const active = new WeakMap<Element, { settle: (reason: 'complete' | 'interrupt') => void }>();

export function beginTransitionSession(
  el: Element,
  _phase: TransitionPhase,
  done?: () => void,
): TransitionSession {
  active.get(el)?.settle('interrupt');

  let settled = false;
  const settle = (_reason: 'complete' | 'interrupt') => {
    if (settled) return;
    settled = true;
    active.delete(el);
    done?.();
  };

  const session: TransitionSession = {
    complete: () => settle('complete'),
    interrupt: () => settle('interrupt'),
  };
  active.set(el, { settle });
  return session;
}

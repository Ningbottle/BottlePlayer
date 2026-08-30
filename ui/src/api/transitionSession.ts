export type TransitionPhase = 'enter' | 'leave';

export interface TransitionSession {
  complete(): void;
  interrupt(): void;
}

interface TransitionStyleSnapshot {
  opacity: string;
  transform: string;
  filter: string;
  pointerEvents: string;
}

interface ActiveTransitionSession {
  settle(reason: 'complete' | 'interrupt'): void;
  restore(): void;
}

const active = new Map<Element, ActiveTransitionSession>();

function snapshotStyles(element: Element): TransitionStyleSnapshot {
  const style = (element as HTMLElement).style;
  return {
    opacity: style.opacity,
    transform: style.transform,
    filter: style.filter,
    pointerEvents: style.pointerEvents,
  };
}

function restoreStyles(element: Element, snapshot: TransitionStyleSnapshot): void {
  const style = (element as HTMLElement).style;
  style.opacity = snapshot.opacity;
  style.transform = snapshot.transform;
  style.filter = snapshot.filter;
  style.pointerEvents = snapshot.pointerEvents;
}

export function beginTransitionSession(
  el: Element,
  phase: TransitionPhase,
  done?: () => void,
): TransitionSession {
  active.get(el)?.settle('interrupt');

  let settled = false;
  const initialStyles = snapshotStyles(el);
  const restore = () => restoreStyles(el, initialStyles);
  const settle = (reason: 'complete' | 'interrupt') => {
    if (settled) return;
    settled = true;
    active.delete(el);
    // A completed leave is commonly cached by Vue KeepAlive rather than
    // destroyed. Restore its inline transition state before Vue deactivates
    // it, otherwise pointer-events:none survives into the next activation.
    if (reason === 'interrupt' || phase === 'leave') restore();
    done?.();
  };

  const session: TransitionSession = {
    complete: () => settle('complete'),
    interrupt: () => settle('interrupt'),
  };
  active.set(el, { settle, restore });
  return session;
}

/** Settle every session that may still own transition styles after a page error. */
export function settleActiveTransitionSessions(): () => void {
  const restorers = [...active.values()].map((session) => session.restore);
  for (const session of [...active.values()]) {
    session.settle('interrupt');
  }
  let restored = false;
  return () => {
    if (restored) return;
    restored = true;
    restorers.forEach((restore) => restore());
  };
}

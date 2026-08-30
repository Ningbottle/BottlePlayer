/**
 * Application page lifecycle — the single owner of the window.pagehide
 * listener. On pagehide it invokes the playback shutdown command supplied by
 * the composition root; it owns no playback state, no media resources, and no
 * business teardown (see pageLifecycle responsibility table / plan Task B3).
 *
 * Re-installation (HMR / app re-composition) replaces the previous handler so
 * exactly one owner exists at any time. Each install returns a disposer with
 * an identity check: a stale disposer is a no-op.
 */

export interface PageLifecycleDeps {
  shutdownPlayback: () => Promise<void>;
}

type BottleMusicLifecycleGlobal = Window & {
  __bottlemusic_page_lifecycle_dispose__?: (() => void) | undefined;
};

function lifecycleGlobal(): BottleMusicLifecycleGlobal {
  return window as unknown as BottleMusicLifecycleGlobal;
}

export function installPageLifecycle(deps: PageLifecycleDeps): () => void {
  const g = lifecycleGlobal();

  // Replace (not stack) any previously installed handler so there is always
  // exactly one pagehide owner.
  const previous = g.__bottlemusic_page_lifecycle_dispose__;
  if (typeof previous === 'function') previous();

  const onPageHide = () => {
    try {
      void Promise.resolve(deps.shutdownPlayback()).catch(() => {
        /* shutdown best-effort: never leak an unhandled rejection */
      });
    } catch {
      /* sync throw from the callback must not escape the pagehide dispatch */
    }
  };

  window.addEventListener('pagehide', onPageHide);

  const dispose = () => {
    // Identity check: only the current owner may remove the handler and
    // clear the slot. A stale disposer must be a no-op.
    if (g.__bottlemusic_page_lifecycle_dispose__ !== dispose) return;
    window.removeEventListener('pagehide', onPageHide);
    g.__bottlemusic_page_lifecycle_dispose__ = undefined;
  };

  g.__bottlemusic_page_lifecycle_dispose__ = dispose;
  return dispose;
}

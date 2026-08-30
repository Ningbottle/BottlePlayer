import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

type BottleMusicLifecycleGlobal = Window & {
  __bottlemusic_page_lifecycle_dispose__?: (() => void) | undefined;
};

function lifecycleGlobal(): BottleMusicLifecycleGlobal {
  return window as unknown as BottleMusicLifecycleGlobal;
}

/** Install via dynamic import so each test controls module state. */
async function install(shutdownPlayback: () => Promise<void>) {
  const { installPageLifecycle } = await import('../pageLifecycle');
  return installPageLifecycle({ shutdownPlayback });
}

/** Remove whatever handler the global slot currently points at. */
function cleanupCurrentHandler() {
  const dispose = lifecycleGlobal().__bottlemusic_page_lifecycle_dispose__;
  if (typeof dispose === 'function') dispose();
  window.removeEventListener('pagehide', expect.any(Function) as never);
}

describe('installPageLifecycle', () => {
  beforeEach(() => {
    vi.resetModules();
    lifecycleGlobal().__bottlemusic_page_lifecycle_dispose__ = undefined;
  });

  afterEach(() => {
    cleanupCurrentHandler();
    vi.restoreAllMocks();
  });

  it('first install: one pagehide triggers shutdownPlayback exactly once', async () => {
    const shutdownPlayback = vi.fn(async () => {});
    const dispose = await install(shutdownPlayback);

    window.dispatchEvent(new Event('pagehide'));
    await Promise.resolve();
    await Promise.resolve();

    expect(shutdownPlayback).toHaveBeenCalledTimes(1);
    expect(typeof dispose).toBe('function');
  });

  it('second install replaces the first handler: one pagehide runs only the second callback, never the first', async () => {
    const first = vi.fn(async () => {});
    const second = vi.fn(async () => {});

    await install(first);
    await install(second);

    window.dispatchEvent(new Event('pagehide'));
    await Promise.resolve();
    await Promise.resolve();

    expect(first).not.toHaveBeenCalled();
    expect(second).toHaveBeenCalledTimes(1);
  });

  it('a stale disposer from a replaced install is a no-op and does not remove the current handler', async () => {
    const first = vi.fn(async () => {});
    const second = vi.fn(async () => {});

    const staleDispose = await install(first);
    await install(second);

    // The old disposer must not tear down the new (current) handler.
    expect(() => staleDispose()).not.toThrow();

    window.dispatchEvent(new Event('pagehide'));
    await Promise.resolve();
    await Promise.resolve();

    expect(second).toHaveBeenCalledTimes(1);
  });

  it('calling the current disposer removes the handler: pagehide no longer triggers the callback', async () => {
    const shutdownPlayback = vi.fn(async () => {});
    const dispose = await install(shutdownPlayback);

    dispose();

    window.dispatchEvent(new Event('pagehide'));
    await Promise.resolve();
    await Promise.resolve();

    expect(shutdownPlayback).not.toHaveBeenCalled();
    expect(lifecycleGlobal().__bottlemusic_page_lifecycle_dispose__).toBeUndefined();
  });

  it('shutdownPlayback synchronous throw does not escape the pagehide dispatch', async () => {
    const shutdownPlayback = vi.fn(() => {
      throw new Error('sync shutdown failure');
    });
    await install(shutdownPlayback);

    expect(() => window.dispatchEvent(new Event('pagehide'))).not.toThrow();
    expect(shutdownPlayback).toHaveBeenCalledTimes(1);
  });

  it('shutdownPlayback Promise rejection does not produce an unhandled rejection', async () => {
    const shutdownPlayback = vi.fn(async () => {
      throw new Error('async shutdown failure');
    });
    await install(shutdownPlayback);

    const unhandled: unknown[] = [];
    const handler = (reason: unknown) => unhandled.push(reason);
    process.on('unhandledRejection', handler);
    try {
      window.dispatchEvent(new Event('pagehide'));
      await new Promise((r) => setTimeout(r, 0));

      expect(shutdownPlayback).toHaveBeenCalledTimes(1);
      expect(unhandled).toHaveLength(0);
    } finally {
      process.off('unhandledRejection', handler);
    }
  });
});

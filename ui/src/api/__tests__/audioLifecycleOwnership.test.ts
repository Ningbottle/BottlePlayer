/**
 * Phase 4 regression tests: AudioContext ownership and HMR/pagehide lifecycle.
 *
 * Drives the R3 fix (analyser AudioContext dispose for HMR) and locks the
 * R9 two-context invariant (EQ closeable + analyser never-close on pagehide).
 *
 * Fix commit: `refactor(audio): add analyser AudioContext dispose for HMR`.
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

vi.mock('../motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

// ── Mocks for audioLevelMonitor (analyser context) ──

interface MockAnalyserCtx {
  state: string;
  close: ReturnType<typeof vi.fn>;
  resume: ReturnType<typeof vi.fn>;
  createAnalyser: ReturnType<typeof vi.fn>;
  createMediaStreamSource: ReturnType<typeof vi.fn>;
}

function setupAnalyserMocks() {
  const closeSpy = vi.fn().mockResolvedValue(undefined);
  const ctx: MockAnalyserCtx = {
    state: 'running',
    close: closeSpy,
    resume: vi.fn().mockResolvedValue(undefined),
    createAnalyser: vi.fn(() => ({
      fftSize: 256,
      smoothingTimeConstant: 0.6,
      getByteTimeDomainData: vi.fn(),
      connect: vi.fn(),
      disconnect: vi.fn(),
    })),
    createMediaStreamSource: vi.fn(() => ({ connect: vi.fn(), disconnect: vi.fn() })),
  };
  function MockAudioContext(this: unknown) {
    return ctx;
  }
  const ctorSpy = vi.fn(MockAudioContext);
  vi.stubGlobal('AudioContext', ctorSpy);

  const stream = {
    getAudioTracks: vi.fn(() => [{ stop: vi.fn() }]),
    getVideoTracks: vi.fn(() => []),
  };
  const capturableAudio = document.createElement('audio') as HTMLAudioElement & {
    captureStream?: () => MediaStream;
  };
  capturableAudio.captureStream = vi.fn(() => stream as unknown as MediaStream);

  return { ctx, closeSpy, ctorSpy, capturableAudio };
}

describe('R3: disposeAudioLevelMonitor for HMR', () => {
  beforeEach(() => {
    vi.resetModules();
  });
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it('disposeAudioLevelMonitor closes the shared AudioContext', async () => {
    const mocks = setupAnalyserMocks();
    const { createAudioLevelMonitor, disposeAudioLevelMonitor } = await import('../audioLevelMonitor');
    const monitor = createAudioLevelMonitor(mocks.capturableAudio);
    monitor.start();

    expect(mocks.closeSpy).not.toHaveBeenCalled();

    disposeAudioLevelMonitor();

    expect(mocks.closeSpy).toHaveBeenCalledTimes(1);
  });

  it('after dispose, a new monitor creates a fresh AudioContext', async () => {
    const mocks = setupAnalyserMocks();
    const { createAudioLevelMonitor, disposeAudioLevelMonitor } = await import('../audioLevelMonitor');
    const m1 = createAudioLevelMonitor(mocks.capturableAudio);
    m1.start();
    expect(mocks.ctorSpy.mock.calls.length).toBe(1);

    disposeAudioLevelMonitor();

    const m2 = createAudioLevelMonitor(mocks.capturableAudio);
    m2.start();
    // A fresh AudioContext was created after dispose.
    expect(mocks.ctorSpy.mock.calls.length).toBe(2);
    m2.stop();
  });

  it('dispose is idempotent (double-dispose does not throw or double-close)', async () => {
    const mocks = setupAnalyserMocks();
    const { createAudioLevelMonitor, disposeAudioLevelMonitor } = await import('../audioLevelMonitor');
    const monitor = createAudioLevelMonitor(mocks.capturableAudio);
    monitor.start();

    disposeAudioLevelMonitor();
    expect(() => disposeAudioLevelMonitor()).not.toThrow();
    // Only the first dispose closes the context.
    expect(mocks.closeSpy).toHaveBeenCalledTimes(1);
  });

  it('swallows async rejection from close() (no unhandled rejection on HMR)', async () => {
    const mocks = setupAnalyserMocks();
    // Override close to reject — simulates a real AudioContext that fails to close.
    mocks.closeSpy.mockRejectedValue(new Error('close failed'));

    // Track unhandled rejections — if .catch() is removed, this will capture one.
    const unhandled: unknown[] = [];
    const handler = (reason: unknown) => unhandled.push(reason);
    process.on('unhandledRejection', handler);

    try {
      const { createAudioLevelMonitor, disposeAudioLevelMonitor } = await import('../audioLevelMonitor');
      const monitor = createAudioLevelMonitor(mocks.capturableAudio);
      monitor.start();

      // Must not throw synchronously.
      expect(() => disposeAudioLevelMonitor()).not.toThrow();

      // Let microtasks settle so the rejected promise would surface if uncaught.
      await new Promise((r) => setTimeout(r, 0));

      expect(mocks.closeSpy).toHaveBeenCalledTimes(1);
      // .catch() swallowed the rejection — no unhandled rejection leaked.
      expect(unhandled).toHaveLength(0);
    } finally {
      process.off('unhandledRejection', handler);
    }
  });
});

// ── B1: single-owner invariants across HMR module generations ──

describe('single-owner invariants across HMR module generations', () => {
  beforeEach(() => {
    vi.resetModules();
    localStorage.clear();
    (window as any).__bottlemusic_audio__ = undefined;
    (window as any).__bottlemusic_player_cleanup__ = undefined;
    (window as any).__bottlemusic_pagehide__ = undefined;
  });

  afterEach(() => {
    const handler = (window as any).__bottlemusic_pagehide__;
    if (typeof handler === 'function') {
      window.removeEventListener('pagehide', handler);
    }
    (window as any).__bottlemusic_audio__ = undefined;
    (window as any).__bottlemusic_player_cleanup__ = undefined;
    (window as any).__bottlemusic_pagehide__ = undefined;
    vi.restoreAllMocks();
  });

  it('one live <audio> and one pagehide owner survive a module re-evaluation; pagehide flushes once', async () => {
    const audioCtorSpy = vi.spyOn(window, 'Audio');

    // Generation 1: creates THE audio element, binds persistence + pagehide.
    const gen1 = await import('../playerStore');
    gen1.initPlayer();
    const sharedAudio = (window as any).__bottlemusic_audio__ as HTMLAudioElement;
    expect(sharedAudio).toBeTruthy();
    expect(audioCtorSpy).toHaveBeenCalledTimes(1);

    gen1.playerStore.queue = [{ FileHash: 'gen-owner', SongName: 'gen-owner' }] as any;
    gen1.playerStore.currentIndex = 0;

    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');

    // Generation 2 (HMR-style re-evaluation): must reuse the SAME element and
    // REPLACE (not stack) the pagehide owner.
    vi.resetModules();
    const gen2 = await import('../playerStore');
    gen2.initPlayer();

    expect(audioCtorSpy).toHaveBeenCalledTimes(1);
    expect((window as any).__bottlemusic_audio__).toBe(sharedAudio);
    expect(gen2.playerStore.audio).toBe(sharedAudio);

    const writesAfterHmr = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_queue_snapshot',
    ).length;

    window.dispatchEvent(new Event('pagehide'));
    await new Promise((r) => setTimeout(r, 20));

    const writesAfterPagehide = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_queue_snapshot',
    ).length;
    // One pagehide owner → exactly one flush. A stacked handler would double this.
    expect(writesAfterPagehide - writesAfterHmr).toBe(1);
    const snap = JSON.parse(localStorage.getItem('player_queue_snapshot') || 'null');
    expect(snap.queue).toEqual([expect.objectContaining({ FileHash: 'gen-owner' })]);

    expect(audioCtorSpy).toHaveBeenCalledTimes(1);
  });
});

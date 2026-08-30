/**
 * Characterization tests for playback runtime lifecycle invariants.
 *
 * These tests lock EXISTING CORRECT behavior that must be preserved across
 * the runtime stability refactor. Each test documents an invariant identified
 * in .superpowers/sdd/runtime-stability-audit.md.
 *
 * Gaps filled (not already covered by other test files):
 * - audioLevelMonitor: stop() must NOT close sharedCtx (the "never close" invariant)
 * - audioLevelMonitor: start() after stop() reuses the same AudioContext
 * - playerPersistence: beforeunload listener calls flushSaveQueue (locked before Phase 5 removal)
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { nextTick } from 'vue';
import { getMediaRuntime } from '../../api/mediaRuntime';

// ── audioLevelMonitor: never-close invariant ──
// Module-level singletons (sharedCtx etc.) persist across tests, so each test
// uses vi.resetModules() + dynamic import for clean state.

vi.mock('../../api/motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

interface MockAnalyser {
  fftSize: number;
  smoothingTimeConstant: number;
  getByteTimeDomainData: ReturnType<typeof vi.fn>;
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
}

interface MockMediaStreamSource {
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
}

interface MockAudioContext {
  state: string;
  currentTime: number;
  destination: unknown;
  resume: ReturnType<typeof vi.fn>;
  close: ReturnType<typeof vi.fn>;
  createAnalyser: ReturnType<typeof vi.fn>;
  createMediaStreamSource: ReturnType<typeof vi.fn>;
}

interface MockStream {
  getAudioTracks: ReturnType<typeof vi.fn>;
  getVideoTracks: ReturnType<typeof vi.fn>;
}

function setupAudioLevelMocks() {
  const closeSpy = vi.fn().mockResolvedValue(undefined);
  const resumeSpy = vi.fn().mockResolvedValue(undefined);

  const analyser: MockAnalyser = {
    fftSize: 256,
    smoothingTimeConstant: 0.6,
    getByteTimeDomainData: vi.fn(),
    connect: vi.fn(),
    disconnect: vi.fn(),
  };

  const source: MockMediaStreamSource = {
    connect: vi.fn(),
    disconnect: vi.fn(),
  };

  const ctx: MockAudioContext = {
    state: 'running',
    currentTime: 0,
    destination: {},
    resume: resumeSpy,
    close: closeSpy,
    createAnalyser: vi.fn(() => analyser),
    createMediaStreamSource: vi.fn(() => source),
  };

  const stream: MockStream = {
    getAudioTracks: vi.fn(() => [{ stop: vi.fn() }]),
    getVideoTracks: vi.fn(() => []),
  };

  const capturableAudio = document.createElement('audio') as HTMLAudioElement & {
    captureStream?: () => MediaStream;
  };
  capturableAudio.captureStream = vi.fn(() => stream as unknown as MediaStream);

  // Must use a `function` declaration (not arrow) so `new AudioContext()` works
  // correctly — arrow functions cannot be used as constructors.
  function MockAudioContext(this: unknown) {
    return ctx;
  }
  const ctorSpy = vi.fn(MockAudioContext);
  vi.stubGlobal('AudioContext', ctorSpy);

  return { ctx, analyser, source, closeSpy, resumeSpy, stream, capturableAudio, ctorSpy };
}

describe('audioLevelMonitor: never-close invariant (R3/R5)', () => {
  beforeEach(() => {
    vi.resetModules();
  });

  afterEach(() => {
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it('stop() does NOT close the shared AudioContext', async () => {
    const mocks = setupAudioLevelMocks();
    const { createAudioLevelMonitor } = await import('../../api/audioLevelMonitor');
    const monitor = createAudioLevelMonitor(mocks.capturableAudio);

    monitor.start();
    monitor.stop();

    // The shared context must stay alive — closing it would blip the output device.
    expect(mocks.closeSpy).not.toHaveBeenCalled();
  });

  it('start() after stop() reuses the same AudioContext (no new context created)', async () => {
    const mocks = setupAudioLevelMocks();
    const { createAudioLevelMonitor } = await import('../../api/audioLevelMonitor');
    const monitor = createAudioLevelMonitor(mocks.capturableAudio);

    monitor.start();
    expect(mocks.ctorSpy.mock.calls.length).toBe(1);
    monitor.stop();

    monitor.start();
    // The module-level singleton must be reused — no new AudioContext constructor call.
    expect(mocks.ctorSpy.mock.calls.length).toBe(1);
    monitor.stop();
  });

  it('two monitors on the same audio element share the same AudioContext', async () => {
    const mocks = setupAudioLevelMocks();
    const { createAudioLevelMonitor } = await import('../../api/audioLevelMonitor');

    const monitor1 = createAudioLevelMonitor(mocks.capturableAudio);
    monitor1.start();

    const monitor2 = createAudioLevelMonitor(mocks.capturableAudio);
    monitor2.start();

    // Only one AudioContext should have been created — the module-level singleton.
    expect(mocks.ctorSpy.mock.calls.length).toBe(1);

    monitor1.stop();
    monitor2.stop();
  });
});

// ── playerPersistence: beforeunload listener removed (R4) ──

describe('playerPersistence: beforeunload listener removed (R4)', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('beforeunload does NOT flush — pagehide is the single flush owner', async () => {
    const { bindQueuePersistence } = await import('../../api/playerPersistence');

    const testQueue = [
      { FileHash: 'p-a', SongName: 'A' },
      { FileHash: 'p-b', SongName: 'B' },
    ];
    bindQueuePersistence(() => ({
      queue: testQueue as any,
      currentIndex: 1,
    }));

    // R4: the module-top-level beforeunload listener was removed.
    // pagehide → disposePlayerRuntime() → flushSaveQueue() is the single owner.
    window.dispatchEvent(new Event('beforeunload'));

    // No flush happened — beforeunload is no longer wired. Persistence uses a
    // single atomic snapshot key (player_queue_snapshot).
    expect(localStorage.getItem('player_queue_snapshot')).toBeNull();
  });
});

// ── playerPersistence: atomic write + non-throwing flush (review R4 P2) ──

describe('playerPersistence: atomic single-key write + non-throwing flush', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('flushSaveQueue writes queue + currentIndex as ONE atomic key (no split writes)', async () => {
    const { bindQueuePersistence, flushSaveQueue } = await import('../../api/playerPersistence');

    const testQueue = [
      { FileHash: 'a', SongName: 'A' },
      { FileHash: 'b', SongName: 'B' },
    ];
    bindQueuePersistence(() => ({ queue: testQueue as any, currentIndex: 1 }));

    // Track every setItem call. Atomic write = exactly ONE call for the
    // snapshot key. If the implementation splits queue/index into two calls,
    // a quota error between them leaves an inconsistent snapshot.
    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');
    const ok = flushSaveQueue();

    expect(ok).toBe(true);
    expect(setItemSpy).toHaveBeenCalledTimes(1);
    expect(setItemSpy).toHaveBeenCalledWith(
      'player_queue_snapshot',
      JSON.stringify({ queue: testQueue, currentIndex: 1 }),
    );
  });

  it('flushSaveQueue returns false and does NOT throw when localStorage.setItem fails', async () => {
    const { bindQueuePersistence, flushSaveQueue } = await import('../../api/playerPersistence');

    bindQueuePersistence(() => ({ queue: [{ FileHash: 'x' }] as any, currentIndex: 0 }));

    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    // Must not throw — callers (HMR cleanup, pagehide) rely on non-throwing.
    expect(() => flushSaveQueue()).not.toThrow();
    expect(flushSaveQueue()).toBe(false);
    // Nothing landed on disk.
    expect(localStorage.getItem('player_queue_snapshot')).toBeNull();
  });

  it('saveQueue debounce callback does NOT throw when setItem fails', async () => {
    vi.useFakeTimers();
    const { bindQueuePersistence, saveQueue } = await import('../../api/playerPersistence');

    bindQueuePersistence(() => ({ queue: [{ FileHash: 'y' }] as any, currentIndex: 0 }));

    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    saveQueue(); // schedules 500ms debounce

    // The debounce timer callback calls flushSaveQueue internally. If that
    // threw, the exception would escape the timer callback (uncaught). Since
    // flushSaveQueue is non-throwing, advancing timers must not throw.
    expect(() => vi.advanceTimersByTime(500)).not.toThrow();
  });

  it('loadQueueSnapshot migrates legacy split keys on first read', async () => {
    // Simulate a pre-upgrade session: data in the old player_queue +
    // player_index keys, nothing in the new snapshot key.
    localStorage.setItem('player_queue', JSON.stringify([{ FileHash: 'legacy' }]));
    localStorage.setItem('player_index', '0');

    const { loadQueueSnapshot } = await import('../../api/playerPersistence');
    const snap = loadQueueSnapshot();

    expect(snap.queue).toHaveLength(1);
    expect((snap.queue[0] as any)?.FileHash).toBe('legacy');
    expect(snap.currentIndex).toBe(0);
  });

  it('loadQueueSnapshot does NOT throw when localStorage.getItem fails (WebView storage unavailable)', async () => {
    // Reviewer concern: if localStorage access itself throws (permission /
    // WebView storage disabled / SecurityError on file://), loadQueueSnapshot
    // must still return safe defaults — not blank-screen the app at module-eval.
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
      throw new Error('SecurityError: Access denied');
    });

    const { loadQueueSnapshot } = await import('../../api/playerPersistence');

    // Must not throw — returns safe empty defaults.
    expect(() => loadQueueSnapshot()).not.toThrow();
    const snap = loadQueueSnapshot();
    expect(snap.queue).toEqual([]);
    expect(snap.currentIndex).toBe(-1);
  });

  it('playerStore module-eval does NOT throw when localStorage.getItem fails for all keys', async () => {
    // Reviewer concern extended: loopMode / queueMode / quality / eqEnabled /
    // activePreset all read localStorage at module-eval. If getItem throws,
    // the whole module import fails → blank page. All reads must be safeGetItem.
    vi.resetModules();
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
      throw new Error('SecurityError: Access denied');
    });

    // Module import triggers playerStore reactive() init with all the reads.
    // If any direct localStorage.getItem remains, the dynamic import rejects.
    const mod = await import('../playerStore');

    // State fell back to safe defaults (no crash, no blank page).
    expect(mod.playerStore.queue).toEqual([]);
    expect(mod.playerStore.currentIndex).toBe(-1);
    expect(mod.playerStore.loopMode).toBe('list');
    expect(mod.playerStore.queueMode).toBe('normal');
    expect(mod.playerStore.quality).toBe('128');
    expect(mod.playerStore.eqEnabled).toBe(false);
    expect(mod.playerStore.activePreset).toBe('Flat');
  });

  it('loadQueueSnapshot normalizes a non-integer atomic snapshot index to -1', async () => {
    localStorage.setItem(
      'player_queue_snapshot',
      JSON.stringify({
        queue: [{ FileHash: 'fractional-index' }],
        currentIndex: 0.5,
      }),
    );

    const { loadQueueSnapshot } = await import('../../api/playerPersistence');
    const snap = loadQueueSnapshot();

    expect(snap.queue).toHaveLength(1);
    expect(snap.currentIndex).toBe(-1);
  });

  it.each(['not-a-number', '1.5', '1abc'])(
    'loadQueueSnapshot normalizes invalid legacy index %s to -1',
    async (legacyIndex) => {
      localStorage.setItem('player_queue', JSON.stringify([{ FileHash: 'legacy-invalid-index' }]));
      localStorage.setItem('player_index', legacyIndex);

      const { loadQueueSnapshot } = await import('../../api/playerPersistence');
      const snap = loadQueueSnapshot();

      expect(snap.queue).toHaveLength(1);
      expect(snap.currentIndex).toBe(-1);
    },
  );

  it('playerStore module-eval rejects invalid persisted playback modes', async () => {
    localStorage.setItem('player_loop_mode', 'broken-loop-mode');
    localStorage.setItem('player_queue_mode', 'broken-queue-mode');
    vi.resetModules();

    const mod = await import('../playerStore');

    expect(mod.playerStore.loopMode).toBe('list');
    expect(mod.playerStore.queueMode).toBe('normal');
  });

  it('playerStore module-eval preserves valid non-default playback modes', async () => {
    localStorage.setItem('player_loop_mode', 'random');
    localStorage.setItem('player_queue_mode', 'personalFm');
    vi.resetModules();

    const mod = await import('../playerStore');

    expect(mod.playerStore.loopMode).toBe('random');
    expect(mod.playerStore.queueMode).toBe('personalFm');
  });

  it('setQuality succeeds when persistence writes are unavailable', async () => {
    vi.resetModules();
    const mod = await import('../playerStore');
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    await expect(mod.setQuality('320')).resolves.toEqual({ status: 'ok' });
    expect(mod.playerStore.quality).toBe('320');
  });

  it('volume updates the audio backend fallback when persistence writes fail', async () => {
    vi.resetModules();
    const mod = await import('../playerStore');
    const { getMediaRuntime } = await import('../../api/mediaRuntime');
    mod.initPlayer();
    const audio = getMediaRuntime()!.audio;
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    mod.playerStore.volume = 0.25;
    await nextTick();

    expect(audio.volume).toBe(0.25);
  });
});

// ── B1: orphan module after vi.resetModules must not take over runtime ownership ──

describe('orphan playerStore module after vi.resetModules (single-owner invariant)', () => {
  beforeEach(() => {
    // Earlier tests in this file import ../playerStore after their own
    // vi.resetModules(); reset again so `live` is a guaranteed-fresh instance
    // (a cached instance may hold a runtime binding from a previous test,
    // which would make initPlayer() early-return).
    vi.resetModules();
    localStorage.clear();
    (window as any).__bottlemusic_media_runtime__ = undefined;
  });

  afterEach(() => {
    (window as any).__bottlemusic_media_runtime__ = undefined;
    vi.restoreAllMocks();
  });

  it('a re-evaluated module that never calls initPlayer cannot take over the runtime or persistence snapshot', async () => {
    // Live generation: owns the MediaRuntime and the persistence snapshot.
    // (Pagehide listener ownership now lives in app/lifecycle; persistence is
    // exercised here via the live module's shutdown command.)
    const live = await import('../playerStore');
    live.initPlayer();
    const liveRuntime = getMediaRuntime();
    const liveAudio = liveRuntime!.audio as HTMLAudioElement;
    expect(liveAudio).toBeTruthy();
    expect(liveRuntime).toBeTruthy();

    live.playerStore.queue = [{ FileHash: 'live-owner', SongName: 'live-owner' }] as any;
    live.playerStore.currentIndex = 0;

    // Orphan generation: fresh module evaluation, initPlayer never called.
    vi.resetModules();
    const orphan = await import('../playerStore');
    expect(orphan.playerStore).not.toBe(live.playerStore);
    // It owns no audio and did not replace the global runtime owner.
    expect(getMediaRuntime()).toBe(liveRuntime);
    expect((window as any).__bottlemusic_media_runtime__?.audio).toBe(liveAudio);

    // A stale/empty orphan state must NOT be what shutdown persists.
    orphan.playerStore.queue = [];
    orphan.playerStore.currentIndex = -1;

    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');
    localStorage.removeItem('player_queue_snapshot');
    await live.disposePlayerRuntime();

    const snapshotWrites = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_queue_snapshot',
    );
    // Exactly one flush — the live module's snapshot, not the orphan's empty queue.
    expect(snapshotWrites).toHaveLength(1);
    const snap = JSON.parse(localStorage.getItem('player_queue_snapshot') || 'null');
    expect(snap.queue).toEqual([expect.objectContaining({ FileHash: 'live-owner' })]);
    expect(snap.currentIndex).toBe(0);
    // The orphan still owns nothing after the flush.
    expect(getMediaRuntime()!.audio).toBe(liveAudio);
  });
});

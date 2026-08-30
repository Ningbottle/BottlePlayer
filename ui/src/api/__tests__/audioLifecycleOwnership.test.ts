/**
 * Phase 4 regression tests: AudioContext ownership and HMR/pagehide lifecycle.
 *
 * Drives the R3 fix (analyser AudioContext dispose for HMR) and locks the
 * R9 two-context invariant (EQ closeable + analyser never-close on pagehide).
 *
 * Fix commit: `refactor(audio): add analyser AudioContext dispose for HMR`.
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { getMediaRuntime, type MediaRuntimeDeps } from '../mediaRuntime';

vi.mock('../motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

/** Fake backend + deps for MediaRuntime contract tests (no real Html5AudioBackend). */
function makeRuntimeDeps() {
  const unsubSpy = vi.fn();
  const backend = {
    kind: 'html5' as const,
    initialize: vi.fn(async () => true),
    playUrl: vi.fn(async () => true),
    switchUrl: vi.fn(async () => true),
    hasSource: vi.fn(() => false),
    pause: vi.fn(async () => {}),
    resume: vi.fn(async () => {}),
    stop: vi.fn(async () => {}),
    seek: vi.fn(async () => {}),
    setVolume: vi.fn(async () => {}),
    setRate: vi.fn(async () => {}),
    getState: vi.fn(async () => ({ state: 'stopped', position: 0, duration: 0 })),
    shutdown: vi.fn(async () => {}),
    onEvent: vi.fn(() => unsubSpy),
  };
  const deps: MediaRuntimeDeps = {
    initialVolume: vi.fn(() => 0.7),
    createBackend: vi.fn(() => backend),
    onBackendEvent: vi.fn(),
    onDuration: vi.fn(),
    onFirstPlay: vi.fn(),
    beforeHmrDetach: vi.fn(),
  };
  return { deps, backend, unsubSpy };
}

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
    (window as any).__bottlemusic_media_runtime__ = undefined;
  });

  afterEach(() => {
    (window as any).__bottlemusic_media_runtime__ = undefined;
    vi.restoreAllMocks();
  });

  it('one live <audio> survives a module re-evaluation; the re-evaluated module flushes once via its shutdown command', async () => {
    const audioCtorSpy = vi.spyOn(window, 'Audio');

    // Generation 1: creates THE audio element and binds persistence.
    const gen1 = await import('../../playback/playerStore');
    gen1.initPlayer();
    const sharedAudio = getMediaRuntime()!.audio as HTMLAudioElement;
    expect(sharedAudio).toBeTruthy();
    expect(audioCtorSpy).toHaveBeenCalledTimes(1);

    gen1.playerStore.queue = [{ FileHash: 'gen-owner', SongName: 'gen-owner' }] as any;
    gen1.playerStore.currentIndex = 0;

    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');

    // Generation 2 (HMR-style re-evaluation): must reuse the SAME element.
    // (Pagehide single-owner/replace behavior now lives in pageLifecycle.test.ts.)
    vi.resetModules();
    const gen2 = await import('../../playback/playerStore');
    gen2.initPlayer();

    expect(audioCtorSpy).toHaveBeenCalledTimes(1);
    expect((window as any).__bottlemusic_media_runtime__?.audio).toBe(sharedAudio);
    expect(getMediaRuntime()!.audio).toBe(sharedAudio);

    const writesAfterHmr = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_queue_snapshot',
    ).length;

    // The live module's shutdown command flushes exactly once.
    await gen2.disposePlayerRuntime();

    const writesAfterShutdown = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_queue_snapshot',
    ).length;
    expect(writesAfterShutdown - writesAfterHmr).toBe(1);
    const snap = JSON.parse(localStorage.getItem('player_queue_snapshot') || 'null');
    expect(snap.queue).toEqual([expect.objectContaining({ FileHash: 'gen-owner' })]);

    expect(audioCtorSpy).toHaveBeenCalledTimes(1);
  });
});

// ── B2: MediaRuntime is the single audio/backend/listener owner ──

describe('mediaRuntime: single global owner contract', () => {
  beforeEach(() => {
    vi.resetModules();
    localStorage.clear();
    (window as any).__bottlemusic_media_runtime__ = undefined;
  });

  afterEach(() => {
    (window as any).__bottlemusic_media_runtime__ = undefined;
    vi.restoreAllMocks();
  });

  it('creates one audio and publishes the single owner slot, but no backend before ensureBackend', async () => {
    const { getOrCreateMediaRuntime, getMediaRuntime } = await import('../mediaRuntime');
    expect(getMediaRuntime()).toBeNull();

    const { deps } = makeRuntimeDeps();
    const runtime = getOrCreateMediaRuntime(deps);

    expect(getMediaRuntime()).toBe(runtime);
    expect(runtime.audio).toBeTruthy();
    // Lazy backend: creating the runtime must NOT create a Backend.
    expect(deps.createBackend).not.toHaveBeenCalled();
    expect(runtime.getBackend()).toBeNull();
  });

  it('ensureBackend() synchronously builds the backend with initialVolume and subscribes backend events', async () => {
    const { getOrCreateMediaRuntime } = await import('../mediaRuntime');
    const { deps, backend, unsubSpy } = makeRuntimeDeps();
    const runtime = getOrCreateMediaRuntime(deps);

    // Sync contract: callers read the backend on the same call stack.
    const wired = runtime.ensureBackend();
    expect(wired).toBe(backend);
    expect(deps.createBackend).toHaveBeenCalledTimes(1);
    expect(deps.createBackend).toHaveBeenCalledWith(runtime.audio, 0.7);
    expect(deps.initialVolume).toHaveBeenCalledTimes(1);
    expect(backend.onEvent).toHaveBeenCalledTimes(1);
    expect(backend.onEvent).toHaveBeenCalledWith(deps.onBackendEvent);

    // Idempotent: a second ensure reuses the same instance.
    expect(runtime.ensureBackend()).toBe(backend);
    expect(deps.createBackend).toHaveBeenCalledTimes(1);
    expect(unsubSpy).not.toHaveBeenCalled();
  });

  it('HMR rebind reuses the SAME audio, runs beforeHmrDetach exactly once, drops the backend ref without pause/src-clear/load', async () => {
    const { getOrCreateMediaRuntime, getMediaRuntime } = await import('../mediaRuntime');
    const gen1 = makeRuntimeDeps();
    const first = getOrCreateMediaRuntime(gen1.deps);
    const audio = first.audio;
    audio.src = 'http://127.0.0.1:17631/audio/hmr-runtime';
    Object.defineProperty(audio, 'currentTime', { value: 42, writable: true, configurable: true });
    const pauseSpy = vi.spyOn(audio, 'pause').mockImplementation(() => {});
    const loadSpy = vi.spyOn(audio, 'load').mockImplementation(() => {});

    first.ensureBackend();

    const gen2 = makeRuntimeDeps();
    const second = getOrCreateMediaRuntime(gen2.deps);

    // Old generation teardown ran exactly once; new runtime owns the SAME element.
    expect(gen1.deps.beforeHmrDetach).toHaveBeenCalledTimes(1);
    expect(second.audio).toBe(audio);
    expect(getMediaRuntime()).toBe(second);
    // Old backend ref dropped together with its event subscription.
    expect(second.getBackend()).toBeNull();
    expect(gen1.unsubSpy).toHaveBeenCalledTimes(1);
    // New runtime does not create a backend until ensureBackend.
    expect(gen2.deps.createBackend).not.toHaveBeenCalled();
    // The element was never paused, reloaded, or scrubbed.
    expect(pauseSpy).not.toHaveBeenCalled();
    expect(loadSpy).not.toHaveBeenCalled();
    expect(audio.src).toContain('/audio/hmr-runtime');
    expect(audio.currentTime).toBe(42);
    // Old media listeners are gone: duration metadata only reaches the new deps.
    Object.defineProperty(audio, 'duration', { value: 200, configurable: true });
    audio.dispatchEvent(new Event('durationchange'));
    expect(gen1.deps.onDuration).not.toHaveBeenCalled();
    expect(gen2.deps.onDuration).toHaveBeenCalledTimes(1);
    expect(gen2.deps.onDuration).toHaveBeenCalledWith(200);
  });

  it('detachForHmr is idempotent: beforeHmrDetach runs exactly once even across repeated rebinds', async () => {
    const { getOrCreateMediaRuntime } = await import('../mediaRuntime');
    const gen1 = makeRuntimeDeps();
    const first = getOrCreateMediaRuntime(gen1.deps);
    first.detachForHmr();
    first.detachForHmr();
    expect(gen1.deps.beforeHmrDetach).toHaveBeenCalledTimes(1);

    const gen2 = makeRuntimeDeps();
    getOrCreateMediaRuntime(gen2.deps);
    // The already-detached old runtime must not run its cleanup a second time.
    expect(gen1.deps.beforeHmrDetach).toHaveBeenCalledTimes(1);
    expect(gen2.deps.beforeHmrDetach).not.toHaveBeenCalled();
  });

  it('shutdown retires the backend exactly once and is safe to repeat', async () => {
    const { getOrCreateMediaRuntime } = await import('../mediaRuntime');
    const { deps, backend, unsubSpy } = makeRuntimeDeps();
    const runtime = getOrCreateMediaRuntime(deps);
    runtime.ensureBackend();

    await runtime.shutdown('shutdown');

    expect(backend.shutdown).toHaveBeenCalledTimes(1);
    expect(unsubSpy).toHaveBeenCalledTimes(1);
    expect(runtime.getBackend()).toBeNull();

    await runtime.shutdown('shutdown');
    expect(backend.shutdown).toHaveBeenCalledTimes(1);
  });

  it('stopAndClearMedia pauses and clears src without load() or currentTime changes', async () => {
    const { getOrCreateMediaRuntime } = await import('../mediaRuntime');
    const { deps } = makeRuntimeDeps();
    const runtime = getOrCreateMediaRuntime(deps);
    const audio = runtime.audio;
    audio.src = 'http://127.0.0.1:17631/audio/stop-clear';
    Object.defineProperty(audio, 'currentTime', { value: 87, writable: true, configurable: true });
    const pauseSpy = vi.spyOn(audio, 'pause').mockImplementation(() => {});
    const loadSpy = vi.spyOn(audio, 'load').mockImplementation(() => {});

    runtime.stopAndClearMedia();

    expect(pauseSpy).toHaveBeenCalledTimes(1);
    // jsdom resolves an empty src property against the base URL, so assert
    // the content attribute the setter actually wrote.
    expect(audio.getAttribute('src')).toBe('');
    expect(loadSpy).not.toHaveBeenCalled();
    expect(audio.currentTime).toBe(87);
  });

  it('stopAndClearMedia swallows internal media errors (caller commands must not break)', async () => {
    const { getOrCreateMediaRuntime } = await import('../mediaRuntime');
    const { deps } = makeRuntimeDeps();
    const runtime = getOrCreateMediaRuntime(deps);
    const audio = runtime.audio;
    vi.spyOn(audio, 'pause').mockImplementation(() => {
      throw new Error('pause rejected');
    });
    // Also make the src setter itself throw: property must be assignable via
    // the accessor path without letting the error escape stopAndClearMedia.
    Object.defineProperty(audio, 'src', {
      set: () => {
        throw new Error('src set rejected');
      },
      get: () => '',
      configurable: true,
    });

    expect(() => runtime.stopAndClearMedia()).not.toThrow();
  });
});

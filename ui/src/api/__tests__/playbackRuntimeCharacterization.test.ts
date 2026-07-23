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

// ── audioLevelMonitor: never-close invariant ──
// Module-level singletons (sharedCtx etc.) persist across tests, so each test
// uses vi.resetModules() + dynamic import for clean state.

vi.mock('../motion', () => ({
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
    const { createAudioLevelMonitor } = await import('../audioLevelMonitor');
    const monitor = createAudioLevelMonitor(mocks.capturableAudio);

    monitor.start();
    monitor.stop();

    // The shared context must stay alive — closing it would blip the output device.
    expect(mocks.closeSpy).not.toHaveBeenCalled();
  });

  it('start() after stop() reuses the same AudioContext (no new context created)', async () => {
    const mocks = setupAudioLevelMocks();
    const { createAudioLevelMonitor } = await import('../audioLevelMonitor');
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
    const { createAudioLevelMonitor } = await import('../audioLevelMonitor');

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
    const { bindQueuePersistence } = await import('../playerPersistence');

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

    // No flush happened — beforeunload is no longer wired.
    expect(localStorage.getItem('player_queue')).toBeNull();
    expect(localStorage.getItem('player_index')).toBeNull();
  });
});

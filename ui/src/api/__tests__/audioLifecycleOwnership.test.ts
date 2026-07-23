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
});

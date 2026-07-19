/**
 * Phase 1 P5 — playback stability harness.
 *
 * Pause/resume after a long wall-clock gap (fake timers), sequential
 * switchTrack storms, and seek storms against the orchestrator + mock backend.
 * Patterns mirror playbackOrchestrator.test.ts.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Track } from '../normalizer';
import {
  PlaybackOrchestrator,
  type PlaybackStateSlice,
  type ResolveTrackResult,
} from '../playbackOrchestrator';
import { transitionPhase } from '../playbackPhase';
import type { DiagEvent } from '../playbackDiagnostics';

function mkTrack(hash: string, name = hash): Track {
  return {
    FileHash: hash,
    SongName: name,
    SingerName: 'Artist',
    Duration: 100,
    Image: 'http://img/',
  } as Track;
}

function makeState(): PlaybackStateSlice {
  return {
    currentTrack: null,
    currentIndex: -1,
    queue: [],
    currentTime: 0,
    duration: 0,
    isPlaying: false,
    isLoading: false,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    quality: '128',
    availableQualities: [],
    playbackPhase: 'idle',
  };
}

function makeHarness() {
  let source = '';
  let position = 0;

  const backend = {
    playUrl: vi.fn(async (url: string) => {
      source = url;
      position = 0;
      return true;
    }),
    stop: vi.fn(async () => {
      source = '';
    }),
    switchUrl: vi.fn(async (url: string, options: { position?: number; autoplay: boolean }) => {
      source = url;
      position = options.position ?? 0;
      return true;
    }),
    hasSource: vi.fn(() => source !== ''),
    resume: vi.fn(async () => {
      if (!source) throw new Error('cannot resume without a source');
    }),
    pause: vi.fn(() => {
      /* no-op on mock */
    }),
    seek: vi.fn(async (seconds: number) => {
      if (!source) throw new Error('cannot seek without a source');
      position = seconds;
    }),
  };

  const state = makeState();
  const resolveTrack = vi.fn<(
    track: Track,
    quality: string,
  ) => Promise<ResolveTrackResult>>(async (track) => ({
    status: 1,
    url: `http://x/${track.FileHash}.mp3`,
    data: { available_qualities: [] },
  }));

  const orchestrator = new PlaybackOrchestrator({
    backend: () => backend,
    playSession: { skip: vi.fn(), intend: vi.fn() },
    resolveTrack,
    fetchCover: vi.fn(async () => null),
    uploadPlayHistory: vi.fn(),
    recordRecentPlayed: vi.fn(),
    recordDiagnostic: vi.fn((_e: Omit<DiagEvent, 'ts'>) => {}),
    getState: () => state,
    patchState: (patch) => Object.assign(state, patch),
    saveQueue: vi.fn(),
  });

  return {
    backend,
    orchestrator,
    resolveTrack,
    state,
    /** Backend-reported position (updated by playUrl/seek/switchUrl). */
    getPosition: () => position,
    getSource: () => source,
  };
}

/** Mirror playerStore.seek: backend seek + store currentTime. */
async function seekOn(h: ReturnType<typeof makeHarness>, seconds: number) {
  await h.backend.seek(seconds);
  h.state.currentTime = seconds;
}

/** User pause while playing: backend pause + phase → paused. */
function pauseWhilePlaying(h: ReturnType<typeof makeHarness>) {
  expect(h.state.playbackPhase).toBe('playing');
  h.backend.pause();
  h.state.isPlaying = false;
  h.state.playbackPhase = transitionPhase(h.state.playbackPhase, 'paused');
}

describe('playback stability harness', () => {
  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  describe('pause 30 minutes then resume', () => {
    beforeEach(() => {
      vi.useFakeTimers({ shouldAdvanceTime: false });
    });

    it('resumes to playing after a 30-minute pause without real sleep', async () => {
      const h = makeHarness();
      const track = mkTrack('stable-30m');

      const playResult = await h.orchestrator.switchTrack(track);
      expect(playResult).toEqual({ status: 'played' });
      expect(h.state.playbackPhase).toBe('playing');
      expect(h.state.currentTrack?.FileHash).toBe('stable-30m');
      expect(h.state.isLoading).toBe(false);
      expect(h.backend.hasSource()).toBe(true);

      // Simulate user pause (playerStore.togglePlay → backend.pause path).
      pauseWhilePlaying(h);
      expect(h.state.playbackPhase).toBe('paused');
      expect(h.backend.pause).toHaveBeenCalled();

      // Advance fake wall clock by 30 minutes — do not sleep for real.
      await vi.advanceTimersByTimeAsync(30 * 60 * 1000);

      // Resume path (togglePlay when not playing → resumeOrReloadCurrent).
      const resumeResult = await h.orchestrator.resumeOrReloadCurrent();

      expect(resumeResult).toEqual({ status: 'played' });
      expect(h.backend.resume).toHaveBeenCalled();
      expect(h.state.playbackPhase).toBe('playing');
      expect(h.state.isLoading).toBe(false);
      expect(h.state.currentTrack?.FileHash).toBe('stable-30m');
      expect(h.state.errorMsg).toBe('');
      // Still the same source — no forced reload after long pause with source.
      expect(h.resolveTrack).toHaveBeenCalledTimes(1);
    });

    it('recovers via switchTrack when source is lost during long pause', async () => {
      const h = makeHarness();
      const track = mkTrack('recover-30m');

      await h.orchestrator.switchTrack(track);
      pauseWhilePlaying(h);

      // Source dropped while paused (e.g. element emptied / tab GC).
      h.backend.hasSource.mockReturnValue(false);

      await vi.advanceTimersByTimeAsync(30 * 60 * 1000);

      const resumeResult = await h.orchestrator.resumeOrReloadCurrent();

      expect(resumeResult).toEqual({ status: 'played' });
      expect(h.state.playbackPhase).toBe('playing');
      expect(h.state.isLoading).toBe(false);
      expect(h.state.currentTrack?.FileHash).toBe('recover-30m');
      // Reload path: resolve again after recovering.
      expect(h.resolveTrack).toHaveBeenCalledTimes(2);
    });
  });

  describe('100 sequential track switches', () => {
    it('ends on the last requested track with clean loading/phase state', async () => {
      const h = makeHarness();
      const N = 100;
      const tracks = Array.from({ length: N }, (_, i) => mkTrack(`sw-${i}`));

      for (let i = 0; i < N; i++) {
        const result = await h.orchestrator.switchTrack(tracks[i]);
        expect(result, `switch #${i} (${tracks[i].FileHash})`).toEqual({
          status: 'played',
        });
      }

      expect(h.state.currentTrack?.FileHash).toBe(`sw-${N - 1}`);
      expect(h.state.isLoading).toBe(false);
      expect(h.state.playbackPhase).toBe('playing');
      expect(['loading', 'resolving']).not.toContain(h.state.playbackPhase);
      expect(h.state.errorMsg).toBe('');
      expect(h.backend.playUrl).toHaveBeenCalledTimes(N);
      expect(h.backend.playUrl).toHaveBeenLastCalledWith(`http://x/sw-${N - 1}.mp3`);
      expect(h.resolveTrack).toHaveBeenCalledTimes(N);
    });
  });

  describe('100 sequential seeks', () => {
    it('applies the last seek position and stays playable without residual loading', async () => {
      const h = makeHarness();
      const track = mkTrack('seek-storm', 'Seek Storm');
      track.Duration = 300;

      await h.orchestrator.switchTrack(track);
      expect(h.state.playbackPhase).toBe('playing');
      expect(h.state.isLoading).toBe(false);

      const N = 100;
      let lastPos = 0;
      for (let i = 0; i < N; i++) {
        // Spread seeks across the track duration (avoid landing only on 0).
        lastPos = ((i * 7) % 280) + 1;
        await seekOn(h, lastPos);
      }

      expect(h.backend.seek).toHaveBeenCalledTimes(N);
      expect(h.getPosition()).toBe(lastPos);
      expect(h.state.currentTime).toBe(lastPos);
      expect(h.state.isLoading).toBe(false);
      expect(h.state.playbackPhase).toBe('playing');
      expect(h.state.currentTrack?.FileHash).toBe('seek-storm');
      expect(h.state.errorMsg).toBe('');

      // Seek while paused should not flip phase or re-enter loading.
      pauseWhilePlaying(h);
      const pausedSeek = 42;
      await seekOn(h, pausedSeek);
      expect(h.getPosition()).toBe(pausedSeek);
      expect(h.state.currentTime).toBe(pausedSeek);
      expect(h.state.playbackPhase).toBe('paused');
      expect(h.state.isLoading).toBe(false);
    });
  });
});

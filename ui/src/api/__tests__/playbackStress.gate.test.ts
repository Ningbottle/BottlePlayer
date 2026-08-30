/**
 * Playback stability gate — real wall-clock stress on PlaybackCommandCoordinator.
 * Not a 2h/24h soak; reports actual duration for the requested command volume.
 *
 * Run:
 *   pnpm exec vitest run src/api/__tests__/playbackStress.gate.test.ts
 *   node scripts/playback-stress.mjs --commands 1000
 */
import { describe, it, expect } from 'vitest';
import {
  PlaybackCommandCoordinator,
  type CoordinatorDeps,
  type CoordinatorState,
} from '../playbackCommandCoordinator';
import type { Track } from '../normalizer';

function mkTrack(i: number): Track {
  return {
    FileHash: `h${i % 20}`,
    SongName: `Song ${i % 20}`,
    SingerName: 'S',
    Duration: 100 + (i % 50),
  } as Track;
}

function createHarness() {
  const state: CoordinatorState = {
    queue: Array.from({ length: 10 }, (_, i) => mkTrack(i)),
    currentIndex: 0,
    currentTrack: mkTrack(0),
    isPlaying: true,
    isLoading: false,
    currentTime: 12,
    duration: 200,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    availableQualities: [{ quality: '128', url: 'http://u/128' }],
    playbackPhase: 'playing',
    queueMode: 'normal',
    loopMode: 'list',
  };

  let playCount = 0;
  let qualityFails = 0;
  let seq = 0;

  const deps: CoordinatorDeps = {
    getState: () => state,
    patchState: (p) => Object.assign(state, p),
    saveQueue: () => {},
    playTrack: async (track) => {
      playCount += 1;
      // Simulated async resolve (yield microtask)
      await Promise.resolve();
      state.currentTrack = track;
      const idx = state.queue.findIndex((t) => t.FileHash === track.FileHash);
      state.currentIndex = idx >= 0 ? idx : state.currentIndex;
      state.isPlaying = true;
      state.isLoading = false;
      state.playbackPhase = 'playing';
      return { status: 'played' };
    },
    switchQuality: async (q) => {
      if (q === 'fail') {
        qualityFails += 1;
        return { status: 'failed', message: 'quality_fail' };
      }
      return { status: 'played' };
    },
    seek: async (s) => {
      state.currentTime = s;
    },
    pause: async () => {
      state.isPlaying = false;
      state.playbackPhase = 'paused';
    },
    resumeOrReload: async () => {
      state.isPlaying = true;
      state.playbackPhase = 'playing';
    },
    invalidatePlaybackIntent: () => ++seq,
    detachPlaybackIntent: () => ++seq,
    stopInvalidatedPlayback: async () => {
      state.isPlaying = false;
      state.isLoading = false;
      state.playbackPhase = 'idle';
    },
    skipSession: () => {},
    hasBackend: () => true,
    stopAndClearMedia: () => {},
  };

  const coordinator = new PlaybackCommandCoordinator(deps);
  return { coordinator, state, stats: () => ({ playCount, qualityFails, seq }) };
}

describe('playback stress gate', () => {
  it('survives 1000 mixed commands without stuck queue or phase contradiction', async () => {
    const { coordinator, state, stats } = createHarness();
    const N = Number(process.env.PLAYBACK_STRESS_COMMANDS || 1000);
    const started = Date.now();

    const pending: Promise<unknown>[] = [];
    for (let i = 0; i < N; i++) {
      const r = i % 20;
      if (r < 6) pending.push(coordinator.dispatch({ type: 'next' }));
      else if (r < 10) pending.push(coordinator.dispatch({ type: 'prev' }));
      else if (r === 10) {
        // Ensure track is in queue so select is meaningful
        const t = mkTrack(i);
        if (!state.queue.some((q) => q.FileHash === t.FileHash)) {
          state.queue = [...state.queue, t];
        }
        pending.push(coordinator.dispatch({ type: 'selectTrack', track: t }));
      } else if (r === 11) {
        pending.push(coordinator.dispatch({ type: 'seek', seconds: i % 180 }));
      } else if (r === 12) {
        pending.push(coordinator.dispatch({ type: 'togglePlay' }));
      } else if (r === 13) {
        pending.push(
          coordinator.dispatch({
            type: 'switchQuality',
            quality: i % 17 === 0 ? 'fail' : '320',
          }),
        );
      } else if (r === 14) {
        pending.push(coordinator.dispatch({ type: 'ended' }));
      } else if (r === 15 && i > 0 && i % 200 === 15) {
        // Rare barrier clear + repopulate (not every cycle)
        pending.push(coordinator.dispatch({ type: 'clearQueue' }));
        pending.push(
          coordinator.dispatch({
            type: 'playAll',
            tracks: Array.from({ length: 10 }, (_, j) => mkTrack(j + i)),
            startIndex: 0,
          }),
        );
      } else if (r === 16) {
        if (state.queue.length > 1) {
          pending.push(coordinator.dispatch({ type: 'removeTrack', index: 0 }));
        } else {
          pending.push(coordinator.dispatch({ type: 'next' }));
        }
      } else {
        pending.push(coordinator.dispatch({ type: 'next' }));
      }

      // Burst: every 50 cmds flush a micro batch
      if (i % 50 === 49) {
        await Promise.all(pending.splice(0));
      }
    }
    await Promise.all(pending);
    await coordinator.dispose();

    const ms = Date.now() - started;
    const s = stats();

    // Invariants after mixed storm
    if (state.queue.length === 0) {
      expect(state.currentTrack).toBeNull();
      expect(state.currentIndex).toBe(-1);
      expect(state.isPlaying).toBe(false);
    } else {
      expect(state.currentIndex).toBeGreaterThanOrEqual(-1);
      expect(state.currentIndex).toBeLessThan(state.queue.length);
    }
    // Phase / flag consistency
    if (state.playbackPhase === 'playing') expect(state.isPlaying).toBe(true);
    if (['idle', 'paused', 'error'].includes(state.playbackPhase)) {
      expect(state.isPlaying).toBe(false);
    }

    // eslint-disable-next-line no-console
    console.log(
      JSON.stringify({
        mode: 'coordinator-stress',
        commands: N,
        wallClockMs: ms,
        playCount: s.playCount,
        qualityFails: s.qualityFails,
        finalPhase: state.playbackPhase,
        queueLen: state.queue.length,
      }),
    );

    expect(ms).toBeLessThan(60_000);
    // Coalesced next/prev still produce a meaningful number of commits
    expect(s.playCount).toBeGreaterThan(10);
  }, 90_000);

  it('pause then resume micro-simulation (not 15min soak)', async () => {
    const { coordinator, state } = createHarness();
    await coordinator.dispatch({ type: 'togglePlay' }); // pause
    expect(state.isPlaying).toBe(false);
    // Simulate short pause window (real 15min soak is manual/nightly only)
    await new Promise((r) => setTimeout(r, 20));
    await coordinator.dispatch({ type: 'togglePlay' }); // resume
    expect(state.playbackPhase === 'playing' || state.isPlaying).toBe(true);
    await coordinator.dispose();
  });
});

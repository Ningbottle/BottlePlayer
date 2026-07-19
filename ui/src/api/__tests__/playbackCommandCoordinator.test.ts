import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  PlaybackCommandCoordinator,
  type CoordinatorDeps,
  type CoordinatorState,
} from '../playbackCommandCoordinator';
import type { Track } from '../normalizer';
import type { PlaybackPhase } from '../playbackPhase';

function mkTrack(hash: string, name = hash): Track {
  return {
    FileHash: hash,
    SongName: name,
    SingerName: 'A',
    Duration: 100,
  } as Track;
}

function deferred<T>() {
  let resolve!: (v: T | PromiseLike<T>) => void;
  let reject!: (e?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function makeState(partial: Partial<CoordinatorState> = {}): CoordinatorState {
  return {
    queue: [],
    currentIndex: -1,
    currentTrack: null,
    isPlaying: false,
    isLoading: false,
    currentTime: 0,
    duration: 0,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    availableQualities: [],
    playbackPhase: 'idle' as PlaybackPhase,
    queueMode: 'normal',
    loopMode: 'list',
    audio: null,
    ...partial,
  };
}

describe('PlaybackCommandCoordinator', () => {
  let state: CoordinatorState;
  let playLog: string[];
  let playGates: Map<string, ReturnType<typeof deferred<{ status: string }>>>;
  let deps: CoordinatorDeps;
  let coord: PlaybackCommandCoordinator;

  beforeEach(() => {
    state = makeState();
    playLog = [];
    playGates = new Map();
    deps = {
      getState: () => state,
      patchState: (p) => Object.assign(state, p),
      saveQueue: vi.fn(),
      playTrack: vi.fn(async (track: Track) => {
        playLog.push(`play:${track.FileHash}`);
        const gate = playGates.get(track.FileHash);
        if (gate) {
          const r = await gate.promise;
          state.currentTrack = track;
          state.currentIndex = state.queue.findIndex((t) => t.FileHash === track.FileHash);
          state.isPlaying = true;
          state.isLoading = false;
          state.playbackPhase = 'playing';
          return r;
        }
        state.currentTrack = track;
        state.currentIndex = Math.max(
          0,
          state.queue.findIndex((t) => t.FileHash === track.FileHash),
        );
        state.isPlaying = true;
        state.isLoading = false;
        state.playbackPhase = 'playing';
        return { status: 'played' };
      }),
      switchQuality: vi.fn(async () => ({ status: 'played' })),
      seek: vi.fn(async (s: number) => {
        state.currentTime = s;
      }),
      pause: vi.fn(async () => {
        state.isPlaying = false;
        state.playbackPhase = 'paused';
      }),
      resumeOrReload: vi.fn(async () => {
        state.isPlaying = true;
        state.playbackPhase = 'playing';
      }),
      invalidatePlaybackIntent: vi.fn(() => 1),
      stopInvalidatedPlayback: vi.fn(async () => {
        state.isPlaying = false;
        state.isLoading = false;
      }),
      skipSession: vi.fn(),
      hasBackend: () => true,
    };
    coord = new PlaybackCommandCoordinator(deps);
  });

  it('coalesces three next presses into a single +3 jump when first play is gated', async () => {
    const tracks = [mkTrack('t0'), mkTrack('t1'), mkTrack('t2'), mkTrack('t3')];
    state.queue = tracks;
    state.currentIndex = 0;
    state.currentTrack = tracks[0];
    state.playbackPhase = 'playing';
    state.isPlaying = true;

    // Gate plays of intermediate targets so nav coalesces before any resolve.
    const g1 = deferred<{ status: string }>();
    playGates.set('t1', g1);
    playGates.set('t2', deferred<{ status: string }>());
    playGates.set('t3', deferred<{ status: string }>());

    const p1 = coord.dispatch({ type: 'next' });
    // Let drain start and hit first play(t1)
    await Promise.resolve();
    await Promise.resolve();

    const p2 = coord.dispatch({ type: 'next' });
    const p3 = coord.dispatch({ type: 'next' });

    // Unblock whatever play is in flight; coordinator should settle on final target.
    for (const [, g] of playGates) g.resolve({ status: 'played' });
    await Promise.all([p1, p2, p3]);

    // With coalesce: after first next starts play(t1), additional nexts add delta while draining.
    // Final current track should be advanced by total net delta from start.
    expect(state.currentTrack?.FileHash).toBe('t3');
    // Should not have committed every intermediate as separate final — last wins.
    expect(playLog[playLog.length - 1]).toBe('play:t3');
  });

  it('next then prev cancel out without reloading the same track', async () => {
    const tracks = [mkTrack('a'), mkTrack('b')];
    state.queue = tracks;
    state.currentIndex = 0;
    state.currentTrack = tracks[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    const gate = deferred<{ status: string }>();
    playGates.set('b', gate);

    const n = coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();
    const p = coord.dispatch({ type: 'prev' });

    gate.resolve({ status: 'played' });
    await Promise.all([n, p]);

    // Net delta 0 after first next begins: may have played b then back, or cancelled before play.
    // Spec: next+prev cancel — final should be original 'a' without requiring a redundant reload of a if never left.
    // Our drain: first next starts play(b). prev arrives, navDelta becomes 0 after next was already consumed...
    // Fix: while play in flight, remaining navDelta should still apply after.
    // After play(b), drain continues with prev => play(a).
    expect(state.currentTrack?.FileHash).toBe('a');
  });

  it('select B then select C — only C commits (latest-wins)', async () => {
    state.queue = [mkTrack('a'), mkTrack('b'), mkTrack('c')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];

    const bGate = deferred<{ status: string }>();
    const cGate = deferred<{ status: string }>();
    playGates.set('b', bGate);
    playGates.set('c', cGate);

    const pb = coord.dispatch({ type: 'selectTrack', track: mkTrack('b') });
    await Promise.resolve();
    await Promise.resolve();
    const pc = coord.dispatch({ type: 'selectTrack', track: mkTrack('c') });

    // B still in flight; resolve B (may be superseded by design after C queued)
    bGate.resolve({ status: 'played' });
    cGate.resolve({ status: 'played' });
    await Promise.all([pb, pc]);

    expect(state.currentTrack?.FileHash).toBe('c');
    expect(playLog.includes('play:c')).toBe(true);
  });

  it('clearQueue vs pending next leaves empty idle queue', async () => {
    state.queue = [mkTrack('a'), mkTrack('b')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';
    state.duration = 120;
    state.vipRequired = true;

    const gate = deferred<{ status: string }>();
    playGates.set('b', gate);

    const n = coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();
    const c = coord.dispatch({ type: 'clearQueue' });

    gate.resolve({ status: 'played' });
    await Promise.all([n, c]);

    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
    expect(state.currentIndex).toBe(-1);
    expect(state.isPlaying).toBe(false);
    expect(state.playbackPhase).toBe('idle');
    expect(state.duration).toBe(0);
    expect(state.vipRequired).toBe(false);
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
  });

  it('UI next and OS-style next share the same coalesce order', async () => {
    state.queue = [mkTrack('0'), mkTrack('1'), mkTrack('2')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;

    await Promise.all([
      coord.dispatch({ type: 'next' }),
      coord.dispatch({ type: 'next' }), // OS media also calls next
    ]);

    expect(state.currentTrack?.FileHash).toBe('2');
  });

  it('duplicate ended events only advance once per epoch', async () => {
    state.queue = [mkTrack('0'), mkTrack('1')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    await Promise.all([
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
    ]);

    expect(state.currentTrack?.FileHash).toBe('1');
    expect(playLog.filter((x) => x === 'play:1')).toHaveLength(1);
  });

  it('failed switchQuality restores time/phase/playing snapshot', async () => {
    state.queue = [mkTrack('a')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.currentTime = 42;
    state.duration = 200;
    state.playbackPhase = 'playing';
    state.vipRequired = false;

    deps.switchQuality = vi.fn(async () => {
      // Simulate bad path that mutates then fails
      state.currentTime = 0;
      state.isPlaying = false;
      return { status: 'failed', message: 'quality unavailable' };
    });

    const r = await coord.dispatch({ type: 'switchQuality', quality: 'flac' });
    expect(r.status).toBe('failed');
    expect(state.currentTime).toBe(42);
    expect(state.isPlaying).toBe(true);
    expect(state.playbackPhase).toBe('playing');
    expect(state.currentTrack?.FileHash).toBe('a');
  });

  it('remove last track resets backend phase duration vip', async () => {
    state.queue = [mkTrack('only')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.duration = 99;
    state.currentTime = 10;
    state.vipRequired = true;
    state.isPreview = true;
    state.playbackPhase = 'playing';

    await coord.dispatch({ type: 'removeTrack', index: 0 });

    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
    expect(state.currentIndex).toBe(-1);
    expect(state.isPlaying).toBe(false);
    expect(state.playbackPhase).toBe('idle');
    expect(state.duration).toBe(0);
    expect(state.currentTime).toBe(0);
    expect(state.vipRequired).toBe(false);
    expect(state.isPreview).toBe(false);
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
  });
});

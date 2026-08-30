import { describe, it, expect, vi, beforeEach } from 'vitest';
import type { Track } from '../../shared/music/track';
import {
  playAll,
  playPersonalFm,
  addToQueue,
  removeFromQueue,
  clearQueue,
  __resetQueueCommandChainForTests,
  type QueueState,
  type PlaybackQueueDeps,
} from '../playbackQueue';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'A', Duration: 100 } as Track;
}

function deferred<T = void>() {
  let resolve!: (value: T | PromiseLike<T>) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function makeState(partial: Partial<QueueState> = {}): QueueState {
  return {
    queue: [],
    currentIndex: -1,
    currentTrack: null,
    isPlaying: false,
    isLoading: false,
    queueMode: 'normal',
    errorMsg: '',
    ...partial,
  };
}

function makeDeps(state: QueueState, overrides: Partial<PlaybackQueueDeps> = {}): PlaybackQueueDeps {
  return {
    getState: () => state,
    saveQueue: vi.fn(),
    playTrack: vi.fn(async () => undefined),
    skipSession: vi.fn(),
    invalidatePlaybackIntent: vi.fn(() => 1),
    stopInvalidatedPlayback: vi.fn(async () => undefined),
    hasBackend: () => true,
    stopAndClearMedia: vi.fn(),
    ...overrides,
  };
}

describe('playbackQueue serial commands + residuals', () => {
  beforeEach(() => {
    __resetQueueCommandChainForTests();
  });

  it('serializes concurrent remove (playTrack) then clear (stop) without interleaving', async () => {
    const a = mkTrack('a');
    const b = mkTrack('b');
    const state = makeState({
      queue: [a, b],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
    });

    const playGate = deferred<void>();
    const stopGate = deferred<void>();
    const order: string[] = [];

    const deps = makeDeps(state, {
      playTrack: vi.fn(async (track: Track) => {
        order.push(`playTrack:start:${track.FileHash}`);
        await playGate.promise;
        order.push(`playTrack:end:${track.FileHash}`);
      }),
      stopInvalidatedPlayback: vi.fn(async () => {
        order.push('stop:start');
        await stopGate.promise;
        order.push('stop:end');
      }),
    });

    const removeP = removeFromQueue(deps, 0);
    const clearP = clearQueue(deps);

    // First command starts in this turn; second is blocked until playTrack finishes.
    expect(order).toEqual(['playTrack:start:b']);
    expect(deps.stopInvalidatedPlayback).not.toHaveBeenCalled();

    playGate.resolve();
    await removeP;
    // clear resumes on a subsequent microtask after the lock is released.
    await vi.waitFor(() => {
      expect(order).toEqual(['playTrack:start:b', 'playTrack:end:b', 'stop:start']);
    });

    stopGate.resolve();
    await clearP;

    expect(order).toEqual([
      'playTrack:start:b',
      'playTrack:end:b',
      'stop:start',
      'stop:end',
    ]);
    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
    expect(state.isLoading).toBe(false);
    expect(state.isPlaying).toBe(false);
  });

  it('serializes clear (stop) then remove without interleaving', async () => {
    const a = mkTrack('a');
    const b = mkTrack('b');
    const state = makeState({
      queue: [a, b],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
    });

    const stopGate = deferred<void>();
    const order: string[] = [];

    const deps = makeDeps(state, {
      playTrack: vi.fn(async (track: Track) => {
        order.push(`playTrack:${track.FileHash}`);
      }),
      stopInvalidatedPlayback: vi.fn(async () => {
        order.push('stop:start');
        await stopGate.promise;
        order.push('stop:end');
      }),
    });

    const clearP = clearQueue(deps);
    const removeP = removeFromQueue(deps, 0);

    expect(order).toEqual(['stop:start']);
    expect(deps.playTrack).not.toHaveBeenCalled();

    stopGate.resolve();
    await Promise.all([clearP, removeP]);

    // clear emptied the queue; remove of index 0 is a no-op after that.
    expect(order).toEqual(['stop:start', 'stop:end']);
    expect(deps.playTrack).not.toHaveBeenCalled();
    expect(state.queue).toEqual([]);
    expect(state.isLoading).toBe(false);
  });

  it('clearQueue leaves idle residuals: no loading/playing/currentTrack/errorMsg', async () => {
    const a = mkTrack('a');
    const state = makeState({
      queue: [a],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
      errorMsg: 'stale error',
      currentTime: 42,
      duration: 180,
      isPreview: true,
      vipRequired: true,
      availableQualities: [{ quality: '320', url: 'old' }],
      playbackPhase: 'playing',
    });
    const deps = makeDeps(state);

    await clearQueue(deps);

    expect(state.queue).toEqual([]);
    expect(state.currentIndex).toBe(-1);
    expect(state.currentTrack).toBeNull();
    expect(state.isPlaying).toBe(false);
    expect(state.isLoading).toBe(false);
    expect(state.errorMsg).toBe('');
    expect(state.currentTime).toBe(0);
    expect(state.duration).toBe(0);
    expect(state.isPreview).toBe(false);
    expect(state.vipRequired).toBe(false);
    expect(state.availableQualities).toEqual([]);
    expect(state.playbackPhase).toBe('idle');
    expect(deps.invalidatePlaybackIntent).toHaveBeenCalled();
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
  });

  it('remove current when queue becomes empty clears residuals and stops backend', async () => {
    const a = mkTrack('a');
    const state = makeState({
      queue: [a],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
      errorMsg: 'load failed',
      currentTime: 24,
      duration: 100,
      isPreview: true,
      vipRequired: true,
      availableQualities: [{ quality: '128', url: 'old' }],
      playbackPhase: 'paused',
    });
    const deps = makeDeps(state);

    await removeFromQueue(deps, 0);

    expect(state.queue).toEqual([]);
    expect(state.currentIndex).toBe(-1);
    expect(state.currentTrack).toBeNull();
    expect(state.isPlaying).toBe(false);
    expect(state.isLoading).toBe(false);
    expect(state.errorMsg).toBe('');
    expect(state.currentTime).toBe(0);
    expect(state.duration).toBe(0);
    expect(state.isPreview).toBe(false);
    expect(state.vipRequired).toBe(false);
    expect(state.availableQualities).toEqual([]);
    expect(state.playbackPhase).toBe('idle');
    expect(deps.skipSession).toHaveBeenCalled();
    expect(deps.invalidatePlaybackIntent).toHaveBeenCalled();
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
    expect(deps.playTrack).not.toHaveBeenCalled();
  });

  it('clearQueue without a backend routes the physical stop through stopAndClearMedia exactly once', async () => {
    const a = mkTrack('a');
    const state = makeState({
      queue: [a],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
    });
    const deps = makeDeps(state, { hasBackend: () => false });

    await clearQueue(deps);

    expect(state.queue).toEqual([]);
    expect(state.currentIndex).toBe(-1);
    expect(state.currentTrack).toBeNull();
    expect(state.isPlaying).toBe(false);
    expect(deps.invalidatePlaybackIntent).toHaveBeenCalled();
    expect(deps.stopAndClearMedia).toHaveBeenCalledTimes(1);
    expect(deps.stopInvalidatedPlayback).not.toHaveBeenCalled();
    expect(deps.saveQueue).toHaveBeenCalled();
  });

  it('remove current last track without a backend routes the physical stop through stopAndClearMedia exactly once', async () => {
    const a = mkTrack('a');
    const state = makeState({
      queue: [a],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
    });
    const deps = makeDeps(state, { hasBackend: () => false });

    await removeFromQueue(deps, 0);

    expect(state.queue).toEqual([]);
    expect(state.currentIndex).toBe(-1);
    expect(state.currentTrack).toBeNull();
    expect(state.isPlaying).toBe(false);
    expect(deps.skipSession).toHaveBeenCalled();
    expect(deps.invalidatePlaybackIntent).toHaveBeenCalled();
    expect(deps.stopAndClearMedia).toHaveBeenCalledTimes(1);
    expect(deps.stopInvalidatedPlayback).not.toHaveBeenCalled();
    expect(deps.saveQueue).toHaveBeenCalled();
  });

  it('with a backend present, queue clear does NOT call the physical stopAndClearMedia fallback', async () => {
    const a = mkTrack('a');
    const state = makeState({
      queue: [a],
      currentIndex: 0,
      currentTrack: a,
      isPlaying: true,
      isLoading: true,
    });
    const deps = makeDeps(state);

    await clearQueue(deps);

    expect(deps.stopInvalidatedPlayback).toHaveBeenCalledTimes(1);
    expect(deps.stopAndClearMedia).not.toHaveBeenCalled();
  });

  it('remove current when queue non-empty plays next via playTrack on serial path', async () => {
    const a = mkTrack('a');
    const b = mkTrack('b');
    const c = mkTrack('c');
    const state = makeState({
      queue: [a, b, c],
      currentIndex: 1,
      currentTrack: b,
      isPlaying: true,
      isLoading: true,
    });
    const playGate = deferred<void>();
    const deps = makeDeps(state, {
      playTrack: vi.fn(async () => {
        await playGate.promise;
      }),
    });

    const removeP = removeFromQueue(deps, 1);

    expect(deps.playTrack).toHaveBeenCalledTimes(1);
    expect(deps.playTrack).toHaveBeenCalledWith(
      expect.objectContaining({ FileHash: 'c' }),
    );
    expect(state.queue.map((t) => t.FileHash)).toEqual(['a', 'c']);
    expect(state.currentIndex).toBe(1);

    playGate.resolve();
    await removeP;
  });

  it('playAll / playPersonalFm / addToQueue also share the serial chain', async () => {
    const state = makeState();
    const playGate = deferred<void>();
    const order: string[] = [];
    const deps = makeDeps(state, {
      playTrack: vi.fn(async (track: Track) => {
        order.push(`play:${track.FileHash}`);
        await playGate.promise;
      }),
    });

    const playAllP = playAll(deps, [mkTrack('x'), mkTrack('y')], 0);
    const addP = addToQueue(deps, mkTrack('z'));
    const fmP = playPersonalFm(deps, [mkTrack('fm1')], 0);

    expect(order).toEqual(['play:x']);
    // add/fm blocked until playAll's playTrack finishes
    expect(state.queue.map((t) => t.FileHash)).toEqual(['x', 'y']);
    expect(state.queueMode).toBe('normal');

    playGate.resolve();
    await Promise.all([playAllP, addP, fmP]);

    expect(order).toEqual(['play:x', 'play:fm1']);
    expect(state.queueMode).toBe('personalFm');
    expect(state.queue.map((t) => t.FileHash)).toEqual(['fm1']);
  });

  it('surfaces errors from enqueued commands to callers without breaking the chain', async () => {
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
      isLoading: true,
    });
    const deps = makeDeps(state, {
      stopInvalidatedPlayback: vi.fn(async () => {
        throw new Error('stop failed');
      }),
    });

    await expect(clearQueue(deps)).rejects.toThrow('stop failed');

    // chain still accepts later work
    await addToQueue(deps, mkTrack('b'));
    expect(state.queue.map((t) => t.FileHash)).toEqual(['b']);
    expect(state.isLoading).toBe(false);
  });
});

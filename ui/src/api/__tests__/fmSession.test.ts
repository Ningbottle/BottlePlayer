import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

// playbackDiagnostics is a no-op dependency for unit tests; stub it so the
// fmSession module can be imported without dragging in the real recorder.
vi.mock('../playbackDiagnostics', () => ({
  playbackDiagnostics: {
    recordEvent: vi.fn(),
    markActivity: vi.fn(),
    reset: vi.fn(),
    getEvents: vi.fn(() => []),
  },
}));

import {
  appendPersonalFmRecommendations,
  getFmSessionState,
  disposeFmSession,
  __resetFmSessionForTests,
} from '../fmSession';
import type { Track } from '../normalizer';

function mkTrack(hash: string): Track {
  return { FileHash: hash, SongName: hash, SingerName: 'A', Duration: 100 } as Track;
}

function fmResponse(hashes: string[]) {
  return {
    status: 1,
    data: {
      song_list: hashes.map((h) => ({
        hash: h,
        songname: h,
        singername: 'Reco',
        duration: 120,
      })),
    },
  };
}

interface MutableFmState {
  currentTrack: Track | null;
  queue: Track[];
  currentIndex: number;
  currentTime: number;
  queueMode: 'normal' | 'personalFm';
}

function makeState(overrides: Partial<MutableFmState> = {}): MutableFmState {
  return {
    currentTrack: null,
    queue: [],
    currentIndex: -1,
    currentTime: 0,
    queueMode: 'personalFm',
    ...overrides,
  };
}

describe('personal FM session', () => {
  beforeEach(() => {
    __resetFmSessionForTests();
    mockApiGet.mockReset();
  });

  afterEach(() => {
    __resetFmSessionForTests();
    vi.useRealTimers();
  });

  it('appends fresh recommendations deduped by FileHash against the existing queue', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['b', 'c'])); // b is a dupe, c is fresh
    const saveQueue = vi.fn();

    const appended = await appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue,
    });

    expect(appended).toBe(true);
    expect(state.queue.map((t) => t.FileHash)).toEqual(['a', 'b', 'c']);
    expect(saveQueue).toHaveBeenCalled();
  });

  it('automatically retries an empty response after one second', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['b']));

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() }),
    ).resolves.toBe(false);
    expect(mockApiGet).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(999);
    expect(mockApiGet).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(1);
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a', 'b']);
  });

  it('invalidates a scheduled retry when the current track or index changes', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    const onRetrySuccess = vi.fn();
    mockApiGet
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['b']));

    await expect(
      appendPersonalFmRecommendations(
        { getState: () => state, saveQueue: vi.fn() },
        { onRetrySuccess },
      ),
    ).resolves.toBe(false);

    state.currentTrack = mkTrack('changed');
    state.currentIndex = 1;
    await vi.advanceTimersByTimeAsync(1_000);

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(onRetrySuccess).not.toHaveBeenCalled();
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a']);
  });

  it('uses 1, 3, and 10 second retry delays, then allows a new external round', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    const saveQueue = vi.fn();
    mockApiGet
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['b']));

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue }),
    ).resolves.toBe(false);
    expect(mockApiGet).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(999);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    await vi.advanceTimersByTimeAsync(1);
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    await vi.advanceTimersByTimeAsync(2_999);
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    await vi.advanceTimersByTimeAsync(1);
    expect(mockApiGet).toHaveBeenCalledTimes(3);
    await vi.advanceTimersByTimeAsync(9_999);
    expect(mockApiGet).toHaveBeenCalledTimes(3);
    await vi.advanceTimersByTimeAsync(1);
    expect(mockApiGet).toHaveBeenCalledTimes(4);
    expect(vi.getTimerCount()).toBe(0);
    expect(getFmSessionState().exhausted).toBe(true);

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue }),
    ).resolves.toBe(true);
    expect(mockApiGet).toHaveBeenCalledTimes(5);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a', 'b']);
    expect(getFmSessionState().exhausted).toBe(false);
  });

  it('recovers from a transport failure in the background', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    const saveQueue = vi.fn();
    const onRetrySuccess = vi.fn();
    mockApiGet
      .mockRejectedValueOnce(new Error('offline'))
      .mockResolvedValueOnce(fmResponse(['b']));

    await expect(
      appendPersonalFmRecommendations(
        { getState: () => state, saveQueue },
        { onRetrySuccess },
      ),
    ).resolves.toBe(false);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(saveQueue).not.toHaveBeenCalled();

    await vi.advanceTimersByTimeAsync(999);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    await vi.advanceTimersByTimeAsync(1);

    expect(mockApiGet).toHaveBeenCalledTimes(2);
    expect(saveQueue).toHaveBeenCalledTimes(1);
    expect(onRetrySuccess).toHaveBeenCalledTimes(1);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a', 'b']);
  });

  it('schedules a status=0 response instead of retrying synchronously', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet
      .mockResolvedValueOnce({ status: 0, error: 'temporary' })
      .mockResolvedValueOnce(fmResponse(['b']));

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() }),
    ).resolves.toBe(false);
    expect(mockApiGet).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(1_000);
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a', 'b']);
  });

  it('reset cancels the pending retry timer and prevents stale work', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['a']));

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() }),
    ).resolves.toBe(false);
    expect(vi.getTimerCount()).toBe(1);

    __resetFmSessionForTests();
    await vi.advanceTimersByTimeAsync(10_000);

    expect(vi.getTimerCount()).toBe(0);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a']);
  });

  it('disposeFmSession cancels the pending retry timer (exit/HMR) and blocks stale retries', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['a'])); // empty after dedupe -> retry
    const saveQueue = vi.fn();

    await expect(
      appendPersonalFmRecommendations({ getState: () => state, saveQueue }),
    ).resolves.toBe(false);
    expect(vi.getTimerCount()).toBe(1); // 1s retry scheduled

    // Exit / HMR disposes the FM session.
    disposeFmSession();
    expect(vi.getTimerCount()).toBe(0);

    // Advancing past every retry delay must NOT trigger any retry fetch/append.
    await vi.advanceTimersByTimeAsync(1_000);
    await vi.advanceTimersByTimeAsync(3_000);
    await vi.advanceTimersByTimeAsync(10_000);

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a']);
    // A fresh external call after dispose may start a new round.
    mockApiGet.mockResolvedValue(fmResponse(['z']));
    const appended = await appendPersonalFmRecommendations({ getState: () => state, saveQueue });
    expect(appended).toBe(true);
    expect(state.queue.map((track) => track.FileHash)).toEqual(['a', 'z']);
  });

  it('dedups duplicate FileHashes within a single FM response', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    // The response lists the same FileHash twice - only one may be appended.
    mockApiGet.mockResolvedValue({
      status: 1,
      data: {
        song_list: [
          { hash: 'c', songname: 'c-one', singername: 'X', duration: 1 },
          { hash: 'c', songname: 'c-two', singername: 'X', duration: 1 },
          { hash: 'd', songname: 'd', singername: 'X', duration: 1 },
        ],
      },
    });

    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });

    expect(state.queue.map((t) => t.FileHash)).toEqual(['a', 'b', 'c', 'd']);
  });

  it('cancels the old retry and starts cleanly when a new FM session replaces the queue', async () => {
    vi.useFakeTimers();
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet
      .mockResolvedValueOnce(fmResponse(['a']))
      .mockResolvedValueOnce(fmResponse(['z']));
    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });
    expect(vi.getTimerCount()).toBe(1);

    state.queue = [mkTrack('x'), mkTrack('y')];
    state.currentIndex = 1;
    state.currentTrack = mkTrack('y');

    const appended = await appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue: vi.fn(),
    });

    expect(appended).toBe(true);
    expect(state.queue.map((t) => t.FileHash)).toEqual(['x', 'y', 'z']);
    await vi.advanceTimersByTimeAsync(10_000);
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    expect(vi.getTimerCount()).toBe(0);
  });

  it('discards an in-flight append when the queue is replaced by a new session before it resolves', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    let resolveFetch!: (value: unknown) => void;
    mockApiGet.mockReturnValue(
      new Promise((resolve) => {
        resolveFetch = resolve;
      }),
    );
    const saveQueue = vi.fn();

    const pending = appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue,
    });

    // While the fetch is in flight, a new FM session replaces the queue.
    state.queue = [mkTrack('x'), mkTrack('y')];
    state.currentTrack = mkTrack('y');
    state.currentIndex = 1;

    resolveFetch(fmResponse(['stale']));
    const appended = await pending;

    expect(appended).toBe(false);
    // The stale recommendation must NOT contaminate the new session's queue.
    expect(state.queue.map((t) => t.FileHash)).toEqual(['x', 'y']);
    expect(saveQueue).not.toHaveBeenCalled();
  });

  it('discards an in-flight append when queueMode leaves personalFm before it resolves', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    let resolveFetch!: (value: unknown) => void;
    mockApiGet.mockReturnValue(
      new Promise((resolve) => {
        resolveFetch = resolve;
      }),
    );

    const pending = appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue: vi.fn(),
    });

    state.queueMode = 'normal'; // user switched to a normal queue mid-fetch

    resolveFetch(fmResponse(['stale']));
    expect(await pending).toBe(false);
    expect(state.queue.map((t) => t.FileHash)).toEqual(['a', 'b']);
  });

  it('coalesces concurrent append calls into a single fetch and reports pending', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    let resolveFetch!: (value: unknown) => void;
    mockApiGet.mockReturnValue(
      new Promise((resolve) => {
        resolveFetch = resolve;
      }),
    );

    const p1 = appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });
    expect(getFmSessionState().pending).toBe(true);
    const p2 = appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });

    expect(mockApiGet).toHaveBeenCalledTimes(1);

    resolveFetch(fmResponse(['c']));
    const [r1, r2] = await Promise.all([p1, p2]);

    expect(r1).toBe(true);
    expect(r2).toBe(true);
    expect(getFmSessionState().pending).toBe(false);
    expect(state.queue.map((t) => t.FileHash)).toEqual(['a', 'b', 'c']);
  });

  it('returns false without fetching when not in personalFm queue mode', async () => {
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
      queueMode: 'normal',
    });
    mockApiGet.mockResolvedValue(fmResponse(['c']));

    const appended = await appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue: vi.fn(),
    });

    expect(appended).toBe(false);
    expect(mockApiGet).not.toHaveBeenCalled();
  });

  it('bumps the session generation when a new FM session starts', async () => {
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['b']));
    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });
    const firstGen = getFmSessionState().generation;

    state.queue = [mkTrack('x')]; // new array -> new session
    state.currentTrack = mkTrack('x');
    mockApiGet.mockResolvedValue(fmResponse(['y']));
    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });

    expect(getFmSessionState().generation).toBeGreaterThan(firstGen);
  });
});

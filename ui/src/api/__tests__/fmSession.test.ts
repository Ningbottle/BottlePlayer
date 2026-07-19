import { describe, it, expect, vi, beforeEach } from 'vitest';

const mockApiGet = vi.fn();
vi.mock('../backend', () => ({
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

  it('marks the session exhausted when a successful response yields no fresh tracks', async () => {
    const state = makeState({
      queue: [mkTrack('a'), mkTrack('b')],
      currentIndex: 1,
      currentTrack: mkTrack('b'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['a', 'b'])); // all dupes

    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });

    expect(getFmSessionState().exhausted).toBe(true);
  });

  it('does not refetch once the session is exhausted', async () => {
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['a'])); // dupe -> exhausted

    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });
    mockApiGet.mockClear();

    const appended = await appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue: vi.fn(),
    });

    expect(appended).toBe(false);
    expect(mockApiGet).not.toHaveBeenCalled();
    expect(getFmSessionState().exhausted).toBe(true);
  });

  it('resets exhaustion when a new FM session starts (queue replaced with a new array)', async () => {
    const state = makeState({
      queue: [mkTrack('a')],
      currentIndex: 0,
      currentTrack: mkTrack('a'),
    });
    mockApiGet.mockResolvedValue(fmResponse(['a'])); // dupe -> exhausted
    await appendPersonalFmRecommendations({ getState: () => state, saveQueue: vi.fn() });
    expect(getFmSessionState().exhausted).toBe(true);

    // New FM session: playPersonalFm replaced the queue with a brand-new array.
    state.queue = [mkTrack('x'), mkTrack('y')];
    state.currentIndex = 1;
    state.currentTrack = mkTrack('y');
    mockApiGet.mockResolvedValue(fmResponse(['z']));

    const appended = await appendPersonalFmRecommendations({
      getState: () => state,
      saveQueue: vi.fn(),
    });

    expect(getFmSessionState().exhausted).toBe(false);
    expect(appended).toBe(true);
    expect(state.queue.map((t) => t.FileHash)).toEqual(['x', 'y', 'z']);
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

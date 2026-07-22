import { beforeEach, describe, expect, it, vi } from 'vitest';

const mockApiGet = vi.fn();
vi.mock('../backend', () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { __resetHomeFeedForTest, useHomeFeedStore } from '../homeFeedStore';

const dailyResponse = (hash: string) => ({
  status: 1,
  data: {
    data: {
      song_list: [{ FileHash: hash, SongName: `Song ${hash}`, SingerName: 'Artist', Duration: 180 }],
    },
  },
});

const playlistResponse = (name: string) => ({
  status: 1,
  data: {
    data: {
      info: [{ specialid: 1, specialname: name, nickname: 'Editor', imgurl: 'https://img/{size}.jpg', playcount: 100 }],
    },
  },
});

function respondToHomeFeed(path: string, query?: { sort?: number }) {
  if (path === '/everyday/recommend') return Promise.resolve(dailyResponse('daily'));
  return Promise.resolve(playlistResponse(query?.sort === 5 ? 'Albums' : 'Playlists'));
}

describe('home feed store', () => {
  beforeEach(() => {
    __resetHomeFeedForTest();
    mockApiGet.mockReset();
    mockApiGet.mockImplementation(respondToHomeFeed);
  });

  it('reuses cached sections when ensureLoaded is called again', async () => {
    const store = useHomeFeedStore();

    await store.ensureLoaded();
    await store.ensureLoaded();

    expect(mockApiGet).toHaveBeenCalledTimes(3);
  });

  it('refreshes only the daily section after a calendar rollover', async () => {
    vi.useFakeTimers();
    try {
      vi.setSystemTime(new Date('2026-07-22T10:00:00'));
      const store = useHomeFeedStore();
      await store.ensureLoaded();
      expect(mockApiGet).toHaveBeenCalledTimes(3);

      vi.setSystemTime(new Date('2026-07-23T09:00:00'));
      await store.ensureLoaded();

      const everydayCalls = mockApiGet.mock.calls.filter(([path]) => path === '/everyday/recommend');
      expect(everydayCalls).toHaveLength(2);
      const playlistCalls = mockApiGet.mock.calls.filter(([path]) => path === '/top/playlist');
      expect(playlistCalls).toHaveLength(2);
    } finally {
      vi.useRealTimers();
    }
  });

  it('supersedes concurrent refresh calls and keeps old data visible', async () => {
    const store = useHomeFeedStore();
    await store.ensureLoaded();
    const oldDaily = store.daily.items;

    let resolveRefresh: (() => void) | undefined;
    const refreshResponse = new Promise<void>((resolve) => {
      resolveRefresh = resolve;
    });
    mockApiGet.mockImplementation(() => refreshResponse.then(() => dailyResponse('fresh')));

    const first = store.refresh();
    const second = store.refresh();

    expect(first).not.toBe(second);
    expect(mockApiGet).toHaveBeenCalledTimes(9);
    expect(store.daily.items).toBe(oldDaily);

    resolveRefresh?.();
    await first;
  });

  it('keeps successful sections when one refresh section fails', async () => {
    const store = useHomeFeedStore();
    await store.ensureLoaded();
    const previousDaily = store.daily.items;
    const newPlaylists = playlistResponse('Fresh playlists');

    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') return Promise.reject(new Error('network down'));
      return Promise.resolve(query?.sort === 5 ? playlistResponse('Fresh albums') : newPlaylists);
    });

    await store.refresh();

    expect(store.daily.items).toBe(previousDaily);
    expect(store.daily.error).toBe('加载失败');
    expect(store.playlists.items).toEqual([
      expect.objectContaining({ specialname: 'Fresh playlists', imgurl: 'https://img/400.jpg' }),
    ]);
  });

  it('keeps cached playlists and albums visible during a failed refresh', async () => {
    const store = useHomeFeedStore();
    await store.ensureLoaded();
    const oldPlaylists = store.playlists.items;
    const oldAlbums = store.albums.items;

    mockApiGet.mockRejectedValue(new Error('refresh unavailable'));
    const refreshing = store.refresh();
    expect(store.playlists.items).toBe(oldPlaylists);
    expect(store.albums.items).toBe(oldAlbums);
    await refreshing;

    expect(store.playlists.items).toBe(oldPlaylists);
    expect(store.albums.items).toBe(oldAlbums);
  });

  it('keeps cached items when refresh responses are HTTP-successful but business-invalid', async () => {
    const store = useHomeFeedStore();
    await store.ensureLoaded();
    const oldDaily = store.daily.items;
    const oldPlaylists = store.playlists.items;
    const oldAlbums = store.albums.items;

    let releaseRefresh!: () => void;
    const refreshGate = new Promise<void>((resolve) => { releaseRefresh = resolve; });
    mockApiGet.mockImplementation((path: string) => refreshGate.then(() => {
      if (path === '/everyday/recommend') {
        return { status: 0, data: { data: { song_list: [] } } };
      }
      if (path === '/top/song') {
        return { status: 0, data: { data: { info: [] } } };
      }
      return { status: 1, data: { data: {} } };
    }));

    const refreshing = store.refresh();
    expect(store.daily.items).toBe(oldDaily);
    expect(store.playlists.items).toBe(oldPlaylists);
    expect(store.albums.items).toBe(oldAlbums);

    releaseRefresh();
    await refreshing;

    expect(store.daily.items).toBe(oldDaily);
    expect(store.playlists.items).toBe(oldPlaylists);
    expect(store.albums.items).toBe(oldAlbums);
  });

  it('does not let a pending daily request block independent playlist sections', async () => {
    let releaseDaily!: () => void;
    const dailyPending = new Promise<void>((resolve) => { releaseDaily = resolve; });
    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') {
        return dailyPending.then(() => dailyResponse('late-daily'));
      }
      return Promise.resolve(playlistResponse(query?.sort === 5 ? 'Albums' : 'Playlists'));
    });

    const store = useHomeFeedStore();
    const loading = store.ensureLoaded();
    await vi.waitFor(() => {
      expect(store.playlists.loaded).toBe(true);
      expect(store.albums.loaded).toBe(true);
      expect(store.playlists.items[0]?.specialname).toBe('Playlists');
      expect(store.albums.items[0]?.specialname).toBe('Albums');
    });
    expect(store.daily.loading).toBe(true);

    releaseDaily();
    await loading;
  });

  it('commits albums even when the playlists request fails', async () => {
    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') return Promise.resolve(dailyResponse('daily'));
      if (query?.sort === 2) return Promise.reject(new Error('playlists down'));
      return Promise.resolve(playlistResponse('Albums available'));
    });

    const store = useHomeFeedStore();
    await store.ensureLoaded();

    expect.soft(store.playlists.error).toBe('加载失败');
    expect.soft(store.playlists.loaded).toBe(false);
    expect.soft(store.albums.error).toBeNull();
    expect.soft(store.albums.loaded).toBe(true);
    expect.soft(store.albums.items).toEqual([
      expect.objectContaining({ specialname: 'Albums available' }),
    ]);
  });

  it('treats business-invalid responses as errors and retries unloaded sections', async () => {
    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') {
        return Promise.resolve({ status: 0, data: { data: { song_list: [] } } });
      }
      if (path === '/top/song') {
        return Promise.resolve({ status: 0, data: { data: { info: [] } } });
      }
      if (path === '/top/playlist') {
        return query?.sort === 2
          ? Promise.resolve({ status: 1, data: { data: {} } })
          : Promise.resolve({ status: 0, data: { data: { info: [] } } });
      }
      return Promise.resolve({ status: 0, data: { data: { info: [] } } });
    });

    const store = useHomeFeedStore();
    await store.ensureLoaded();

    expect.soft(store.daily.error).toBeTruthy();
    expect.soft(store.daily.loaded).toBe(false);
    expect.soft(store.playlists.error).toBeTruthy();
    expect.soft(store.playlists.loaded).toBe(false);
    expect.soft(store.albums.error).toBeTruthy();
    expect.soft(store.albums.loaded).toBe(false);

    const failedCalls = mockApiGet.mock.calls.length;
    mockApiGet.mockImplementation(respondToHomeFeed);
    await store.ensureLoaded();
    expect.soft(mockApiGet.mock.calls.length).toBeGreaterThan(failedCalls);
    expect.soft(store.daily.loaded).toBe(true);
    expect.soft(store.playlists.loaded).toBe(true);
    expect.soft(store.albums.loaded).toBe(true);
  });

  it('retries only the requested section', async () => {
    const store = useHomeFeedStore() as typeof useHomeFeedStore extends () => infer S ? S & {
      retrySection: (section: 'daily' | 'playlists' | 'albums') => Promise<void>;
    } : never;

    expect(typeof store.retrySection).toBe('function');
    mockApiGet.mockClear();
    await store.retrySection('playlists');

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(mockApiGet).toHaveBeenCalledWith('/top/playlist', { pagesize: 5, sort: 2 });
  });

  it('accepts explicitly successful empty section lists', async () => {
    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') {
        return Promise.resolve({ status: 1, data: { data: { song_list: [] } } });
      }
      if (path === '/top/song') {
        return Promise.resolve({ status: 1, data: { data: { info: [] } } });
      }
      return Promise.resolve({
        status: 1,
        data: { data: { info: query?.sort === 5 ? [] : [] } },
      });
    });

    const store = useHomeFeedStore();
    await store.ensureLoaded();

    for (const section of [store.daily, store.playlists, store.albums]) {
      expect(section.items).toEqual([]);
      expect(section.loaded).toBe(true);
      expect(section.error).toBeNull();
    }
  });

  it('prevents a stale refresh failure from clearing a newer daily session', async () => {
    const store = useHomeFeedStore() as typeof useHomeFeedStore extends () => infer S ? S & {
      retrySection: (section: 'daily' | 'playlists' | 'albums') => Promise<void>;
    } : never;
    await store.ensureLoaded();

    let releaseOlderDaily!: () => void;
    const olderDaily = new Promise<never>((_resolve, reject) => {
      releaseOlderDaily = () => reject(new Error('older refresh failed'));
    });
    let dailyCalls = 0;
    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      if (path === '/everyday/recommend') {
        dailyCalls += 1;
        return dailyCalls === 1 ? olderDaily : Promise.resolve(dailyResponse('newer-daily'));
      }
      return Promise.resolve(playlistResponse(query?.sort === 5 ? 'Fresh albums' : 'Fresh playlists'));
    });

    const olderRefresh = store.refresh();
    await vi.waitFor(() => expect(store.daily.refreshing).toBe(true));
    await store.retrySection('daily');

    expect(store.daily.items[0]?.FileHash).toBe('newer-daily');
    expect(store.daily.error).toBeNull();
    expect(store.daily.loading).toBe(false);
    expect(store.daily.refreshing).toBe(false);

    releaseOlderDaily();
    await olderRefresh;

    expect(store.daily.items[0]?.FileHash).toBe('newer-daily');
    expect(store.daily.error).toBeNull();
    expect(store.daily.loading).toBe(false);
    expect(store.daily.refreshing).toBe(false);
  });

  it('supersedes every pending section when refresh starts a newer generation', async () => {
    const store = useHomeFeedStore();
    const olderRequests = new Map<string, { reject: (error: Error) => void }>();
    let useFreshResponses = false;

    mockApiGet.mockImplementation((path: string, query?: { sort?: number }) => {
      const key = `${path}:${query?.sort ?? ''}`;
      if (useFreshResponses) {
        if (path === '/everyday/recommend') return Promise.resolve(dailyResponse('fresh-daily'));
        return Promise.resolve(playlistResponse(query?.sort === 5 ? 'Fresh albums' : 'Fresh playlists'));
      }
      return new Promise((_resolve, reject) => {
        olderRequests.set(key, { reject });
      });
    });

    const initialLoad = store.ensureLoaded();
    await vi.waitFor(() => expect(mockApiGet).toHaveBeenCalledTimes(3));

    useFreshResponses = true;
    const refreshed = store.refresh();
    await vi.waitFor(() => expect(mockApiGet).toHaveBeenCalledTimes(6));
    await refreshed;

    expect(store.daily.items[0]?.FileHash).toBe('fresh-daily');
    expect(store.playlists.items[0]?.specialname).toBe('Fresh playlists');
    expect(store.albums.items[0]?.specialname).toBe('Fresh albums');
    expect(store.daily.loading).toBe(false);
    expect(store.playlists.loading).toBe(false);
    expect(store.albums.loading).toBe(false);

    for (const request of olderRequests.values()) {
      request.reject(new Error('stale request failed'));
    }
    await initialLoad;

    expect(store.daily.error).toBeNull();
    expect(store.playlists.error).toBeNull();
    expect(store.albums.error).toBeNull();
    expect(store.daily.refreshing).toBe(false);
    expect(store.playlists.refreshing).toBe(false);
    expect(store.albums.refreshing).toBe(false);
  });

  it('seeds stable synchronous demo data from every public load entry point', async () => {
    const originalUrl = new URL(window.location.href);
    window.history.replaceState({}, '', `${originalUrl.pathname}?layoutDemo=1`);

    try {
      const store = useHomeFeedStore();
      const entryPoints = [
        () => store.ensureLoaded(),
        () => store.refresh(),
        () => store.retrySection('albums'),
      ];

      for (const load of entryPoints) {
        __resetHomeFeedForTest();
        mockApiGet.mockClear();
        const result = load();

        expect(store.daily.items[0]?.FileHash).toBe('demo-track-1');
        expect(store.playlists.items[0]?.specialname).toBe('精选歌单 1');
        expect(store.albums.items[0]?.specialname).toBe('最新歌单 1');
        expect(mockApiGet).not.toHaveBeenCalled();
        await result;
      }
    } finally {
      window.history.replaceState({}, '', `${originalUrl.pathname}${originalUrl.search}`);
      __resetHomeFeedForTest();
    }
  });
});

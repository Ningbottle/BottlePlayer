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

  it('deduplicates concurrent refresh calls and keeps old data visible', async () => {
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

    expect(first).toBe(second);
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
});

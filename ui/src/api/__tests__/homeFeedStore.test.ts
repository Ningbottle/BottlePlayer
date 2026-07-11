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
});

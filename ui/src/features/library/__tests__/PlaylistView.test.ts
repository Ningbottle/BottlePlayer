import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';
import {
  __resetFavoriteMarkersForTests,
  isFavoriteMarker,
} from '../favoriteMarkers';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../../../platform/tauri/nativeClient')>();
  return {
    ...actual,
    apiGet: (...args: any[]) => mockApiGet(...args),
  };
});
vi.mock('../../../playback/playerStore', () => ({
  playAll: vi.fn(),
  playerStore: { currentTrack: null },
}));

import PlaylistView from '../PlaylistView.vue';
import { favoriteStore } from '../favoriteStore';
import { __resetFavoriteRepositoryForTests } from '../favoriteRepository';
import { userStore } from '../../account';
import { playAll } from '../../../playback/index';

function deferred<T>() {
  let resolve!: (value: T | PromiseLike<T>) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

const trackA = {
  FileHash: 'hash-a',
  SongName: 'Song A',
  SingerName: 'Artist A',
  Duration: 100,
};

const trackB = {
  FileHash: 'hash-b',
  SongName: 'Song B',
  SingerName: 'Artist B',
  Duration: 200,
};

describe('PlaylistView skin header', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    vi.mocked(playAll).mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: { list: [], total: 0 } });
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('uses SkinPageHeader instead of legacy page-head', async () => {
    wrapper = mount(PlaylistView, {
      props: { playlistId: '1', playlistName: 'Demo' },
    });
    await flushPromises();

    expect(wrapper.find('.page-head').exists()).toBe(false);
    expect(wrapper.find('.skin-page-header').exists()).toBe(true);
    expect(wrapper.find('.skin-page-header-title').text()).toContain('Demo');
    expect(wrapper.find('.skin-page-header-kicker').text()).toMatch(/PLAYLIST/i);
  });
});

describe('PlaylistView request generation', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    vi.mocked(playAll).mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    vi.mocked(playAll).mockReset();
  });

  it('ignores a stale playlist response after a newer playlistId resolves', async () => {
    const a = deferred<{ status: number; data: { list: typeof trackA[]; total: number } }>();
    const b = deferred<{ status: number; data: { list: typeof trackB[]; total: number } }>();

    mockApiGet
      .mockImplementationOnce(() => a.promise)
      .mockImplementationOnce(() => b.promise);

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await Promise.resolve();

    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await Promise.resolve();

    // B resolves first
    b.resolve({ status: 1, data: { list: [trackB], total: 1 } });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('Song A');
    expect(wrapper.find('.spinner').exists()).toBe(false);

    // Stale A must not overwrite B
    a.resolve({ status: 1, data: { list: [trackA], total: 1 } });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('Song A');
    expect(wrapper.find('.spinner').exists()).toBe(false);
  });

  it('marks tracks only for the account-verified liked playlist ID', async () => {
    __resetFavoriteMarkersForTests();
    __resetFavoriteRepositoryForTests();
    userStore.isLoggedIn = true;
    userStore.userId = 'u1';
    const likedId = 'collection_3_u1_999_0';
    let showTracks = false;
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/playlist') return Promise.resolve({
        status: 1, data: { info: [{ global_collection_id: likedId, listid: '999', listname: '我喜欢的音乐' }] },
      });
      return Promise.resolve({ status: 1, data: { list: showTracks ? [trackA, trackB] : [], total: showTracks ? 2 : 0 } });
    });
    try {
      await favoriteStore.onLogin('u1');
      expect(isFavoriteMarker('hash-a')).toBe(false); // not already set by reconcile
      showTracks = true;
      wrapper = mount(PlaylistView, {
        props: { playlistId: likedId, playlistName: '我喜欢的音乐' },
      });
      await flushPromises();
      expect(isFavoriteMarker('hash-a')).toBe(true);
      expect(isFavoriteMarker('hash-b')).toBe(true);
    } finally {
      wrapper?.unmount();
      wrapper = undefined;
      favoriteStore.onLogout();
      userStore.isLoggedIn = false;
    }
  });

  it('does not mark a public playlist just because it shares the liked name', async () => {
    __resetFavoriteMarkersForTests();
    __resetFavoriteRepositoryForTests();
    userStore.isLoggedIn = true;
    userStore.userId = 'u1';
    let showTracks = false;
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/playlist') return Promise.resolve({
        status: 1, data: { info: [{ global_collection_id: 'collection_3_u1_999_0', listid: '999', listname: '我喜欢的音乐' }] },
      });
      return Promise.resolve({ status: 1, data: { list: showTracks ? [trackA] : [], total: showTracks ? 1 : 0 } });
    });
    try {
      await favoriteStore.onLogin('u1');
      expect(isFavoriteMarker('hash-a')).toBe(false);
      showTracks = true;
      wrapper = mount(PlaylistView, {
        props: { playlistId: 'public-99', playlistName: '我喜欢的音乐' },
      });
      await flushPromises();
      expect(isFavoriteMarker('hash-a')).toBe(false);
    } finally {
      wrapper?.unmount();
      wrapper = undefined;
      favoriteStore.onLogout();
      userStore.isLoggedIn = false;
    }
  });

  it('does not mark tracks from ordinary playlists', async () => {
    __resetFavoriteMarkersForTests();
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { list: [trackA], total: 1 },
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-other', playlistName: '通勤精选' },
    });
    await flushPromises();

    expect(isFavoriteMarker('hash-a')).toBe(false);
  });

  it('does not double-fetch when playlistId changes while page > 1', async () => {
    const pageful = Array.from({ length: 50 }, (_, i) => ({
      ...trackA,
      FileHash: `hash-${i}`,
      SongName: `Song ${i}`,
    }));
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { list: pageful, total: 200 },
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await flushPromises();

    const next = wrapper.findAll('button').find((b) => /Next/i.test(b.text()));
    expect(next).toBeTruthy();
    await next!.trigger('click');
    await flushPromises();

    mockApiGet.mockClear();
    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await flushPromises();

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(mockApiGet.mock.calls[0][0]).toBe('/playlist/track/all');
    expect(mockApiGet.mock.calls[0][1]).toMatchObject({ id: 'pl-b', page: 1 });
  });

  it('ignores a stale playlist error after a newer playlistId succeeds', async () => {
    const a = deferred<{ status: number; data: { list: typeof trackA[]; total: number } }>();
    const b = deferred<{ status: number; data: { list: typeof trackB[]; total: number } }>();

    mockApiGet
      .mockImplementationOnce(() => a.promise)
      .mockImplementationOnce(() => b.promise);

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await Promise.resolve();

    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await Promise.resolve();

    b.resolve({ status: 1, data: { list: [trackB], total: 1 } });
    await flushPromises();

    a.reject(new Error('network down for pl-a'));
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('连接 C++ 后端 Sidecar 出错');
    expect(wrapper.find('.spinner').exists()).toBe(false);
  });

  it('releases queue loading and ignores the old full-list result after changing playlists', async () => {
    const oldFullList = deferred<{ status: number; data: { list: typeof trackA[]; total: number } }>();
    let playlistACalls = 0;
    mockApiGet.mockImplementation((path: string, params: { id?: string }) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      if (params.id === 'pl-a') {
        playlistACalls += 1;
        if (playlistACalls === 1) {
          return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
        }
        return oldFullList.promise;
      }
      return Promise.resolve({ status: 1, data: { list: [trackB], total: 1 } });
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await flushPromises();

    await wrapper.get('[data-test="playlist-play-all"]').trigger('click');
    expect(wrapper.find('[data-test="playlist-cancel-queue"]').exists()).toBe(true);

    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.find('[data-test="playlist-cancel-queue"]').exists()).toBe(false);
    expect(wrapper.get('[data-test="playlist-play-all"]').attributes('disabled')).toBeUndefined();

    oldFullList.resolve({ status: 1, data: { list: [trackA], total: 1 } });
    await flushPromises();

    expect(playAll).not.toHaveBeenCalled();
    expect(wrapper.get('[data-test="playlist-play-all"]').attributes('disabled')).toBeUndefined();
  });

  it('cancels a pending queue walk and never starts playback from its late response', async () => {
    const fullList = deferred<{ status: number; data: { list: typeof trackA[]; total: number } }>();
    let playlistCalls = 0;
    mockApiGet.mockImplementation((path: string) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      playlistCalls += 1;
      if (playlistCalls === 1) return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      return fullList.promise;
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-cancel', playlistName: 'Playlist to cancel' },
    });
    await flushPromises();
    await wrapper.get('[data-test="playlist-play-all"]').trigger('click');
    await wrapper.get('[data-test="playlist-cancel-queue"]').trigger('click');

    expect(wrapper.find('[data-test="playlist-cancel-queue"]').exists()).toBe(false);
    expect(wrapper.text()).toContain('完整歌单加载已取消');
    expect(wrapper.get('[data-test="playlist-play-all"]').attributes('disabled')).toBeUndefined();

    fullList.resolve({ status: 1, data: { list: [trackA], total: 1 } });
    await flushPromises();

    expect(playAll).not.toHaveBeenCalled();
    expect(wrapper.text()).toContain('完整歌单加载已取消');
  });

  it('reports a later-page failure before playing the collected partial queue', async () => {
    const firstPage = Array.from({ length: 50 }, (_, index) => ({
      ...trackA,
      FileHash: `partial-${index}`,
      SongName: `Partial song ${index}`,
    }));
    let trackCalls = 0;
    mockApiGet.mockImplementation((path: string) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      trackCalls += 1;
      if (trackCalls <= 2) {
        return Promise.resolve({ status: 1, data: { list: firstPage, total: 120 } });
      }
      return Promise.resolve({ status: 0, error: '第二页暂时不可用' });
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-partial', playlistName: 'Partial playlist' },
    });
    await flushPromises();
    await wrapper.get('[data-test="playlist-play-all"]').trigger('click');
    await flushPromises();

    expect(wrapper.get('[data-test="playlist-queue-notice"]').text()).toContain('第二页暂时不可用');
    expect(wrapper.get('[data-test="playlist-queue-notice"]').text()).toContain('已排入 50 首');
    expect(playAll).toHaveBeenCalledTimes(1);
    expect(playAll).toHaveBeenCalledWith(expect.arrayContaining([
      expect.objectContaining({ FileHash: 'partial-0' }),
      expect.objectContaining({ FileHash: 'partial-49' }),
    ]), 0);

    // A transient page failure must not cache the partial list as complete.
    // Once the server recovers, pressing play-all again should retry the walk.
    const recovered = Array.from({ length: 120 }, (_, index) => ({
      ...trackA, FileHash: `partial-${index}`, SongName: `Partial song ${index}`,
    }));
    mockApiGet.mockImplementation((path: string, params?: { page: number; pagesize: number }) => {
      if (path !== '/playlist/track/all' || !params) return Promise.resolve({ status: 1, data: { info: [] } });
      const start = (params.page - 1) * params.pagesize;
      return Promise.resolve({ status: 1, data: { list: recovered.slice(start, start + params.pagesize), total: 120 } });
    });
    await wrapper.get('[data-test="playlist-play-all"]').trigger('click');
    await flushPromises();
    const playCalls = vi.mocked(playAll).mock.calls;
    expect(playCalls[playCalls.length - 1]?.[0]).toHaveLength(120);
    expect(wrapper.find('[data-test="playlist-queue-notice"]').exists()).toBe(false);
  });

  it('invalidates a pending queue walk when the account changes', async () => {
    const oldFullList = deferred<{ status: number; data: { list: typeof trackA[]; total: number } }>();
    let trackCalls = 0;
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/playlist') return Promise.resolve({ status: 1, data: { info: [] } });
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
      trackCalls += 1;
      if (trackCalls === 1) return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      if (trackCalls === 2) return oldFullList.promise;
      return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
    });

    try {
      await favoriteStore.onLogin('account-a');
      wrapper = mount(PlaylistView, {
        props: { playlistId: 'public-playlist', playlistName: 'Public' },
      });
      await flushPromises();
      await wrapper.get('[data-test="playlist-play-all"]').trigger('click');

      await favoriteStore.onLogin('account-b');
      await flushPromises();

      expect(wrapper.find('[data-test="playlist-cancel-queue"]').exists()).toBe(false);
      expect(wrapper.get('[data-test="playlist-play-all"]').attributes('disabled')).toBeUndefined();

      oldFullList.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      await flushPromises();
      expect(playAll).not.toHaveBeenCalled();
    } finally {
      favoriteStore.onLogout();
      userStore.isLoggedIn = false;
      userStore.userId = '';
    }
  });
});

describe('PlaylistView failure layers', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('shows 无法获取歌单曲目 for a business status=0 body', async () => {
    mockApiGet.mockResolvedValue({ status: 0, error: '无法获取歌单曲目', data: { list: [], total: 0 } });
    wrapper = mount(PlaylistView, {
      props: { playlistId: '12345', playlistName: 'Public' },
    });
    await flushPromises();
    expect(wrapper.text()).toContain('无法获取歌单曲目');
    expect(wrapper.text()).not.toContain('请稍后重试');
  });

  it('shows a transport copy when the request throws circuit_open', async () => {
    mockApiGet.mockRejectedValue(new Error('circuit_open'));
    wrapper = mount(PlaylistView, {
      props: { playlistId: '12345', playlistName: 'Public' },
    });
    await flushPromises();
    expect(wrapper.text()).toContain('服务暂时繁忙');
    expect(wrapper.text()).not.toContain('无法获取歌单曲目');
  });

  it('shows the empty playlist copy for a successful empty list', async () => {
    mockApiGet.mockResolvedValue({ status: 1, data: { list: [], total: 0 } });
    wrapper = mount(PlaylistView, {
      props: { playlistId: '12345', playlistName: 'Public' },
    });
    await flushPromises();
    expect(wrapper.text()).toContain('该歌单暂无曲目记录');
  });

  it('does not fetch tracks for a user playlist that is only a numeric listid', async () => {
    wrapper = mount(PlaylistView, {
      props: {
        playlistId: '98765',
        playlistName: '收藏歌单',
        playlistSource: 'user',
      },
    });
    await flushPromises();
    expect(mockApiGet).not.toHaveBeenCalled();
    expect(wrapper.text()).toContain('歌单标识无效（缺少 global_collection_id）');
  });

  it('fetches user playlist tracks by global_collection_id, not numeric listid', async () => {
    mockApiGet.mockResolvedValue({ status: 1, data: { list: [trackA], total: 1 } });
    wrapper = mount(PlaylistView, {
      props: {
        playlistId: 'collection_3_42_98765_0',
        playlistName: '收藏歌单',
        playlistSource: 'user',
      },
    });
    await flushPromises();
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(mockApiGet).toHaveBeenCalledWith(
      '/playlist/track/all',
      expect.objectContaining({ id: 'collection_3_42_98765_0' }),
    );
    expect(wrapper.text()).toContain('Song A');
  });
});

describe('PlaylistView loaded-list identity', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    vi.mocked(playAll).mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('drops the previous playlist rows and actions when another playlist fails to load', async () => {
    mockApiGet.mockImplementation((path: string, params?: { id?: string }) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      if (params?.id === 'pl-a') return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      return Promise.resolve({ status: 0, error: '歌单 B 暂时不可用' });
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await flushPromises();
    expect(wrapper.text()).toContain('Song A');
    expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(true);

    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await flushPromises();

    expect(wrapper.text()).toContain('歌单 B 暂时不可用');
    // pl-a rows must not stay countable or queueable under pl-b's title.
    expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(false);
    expect(wrapper.text()).not.toContain('曲目数');
    expect(playAll).not.toHaveBeenCalled();
  });

  it('clears the stale list while a new playlist loads and republishes on success', async () => {
    const pending = deferred<{ status: number; data: { list: typeof trackB[]; total: number } }>();
    mockApiGet.mockImplementation((path: string, params?: { id?: string }) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      if (params?.id === 'pl-a') return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      return pending.promise;
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-a', playlistName: 'Playlist A' },
    });
    await flushPromises();

    await wrapper.setProps({ playlistId: 'pl-b', playlistName: 'Playlist B' });
    await Promise.resolve();

    expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(false);
    expect(wrapper.text()).not.toContain('曲目数');

    pending.resolve({ status: 1, data: { list: [trackB], total: 1 } });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).toContain('曲目数');
    expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(true);
  });

  it('drops rows fetched under another account when the same playlist id reloads', async () => {
    let trackCalls = 0;
    mockApiGet.mockImplementation((path: string) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      trackCalls += 1;
      if (trackCalls === 1) return Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
      return Promise.resolve({ status: 0, error: '账号切换后暂不可读' });
    });

    try {
      await favoriteStore.onLogin('account-a');
      wrapper = mount(PlaylistView, {
        props: { playlistId: 'shared-id', playlistName: 'Shared' },
      });
      await flushPromises();
      expect(wrapper.text()).toContain('Song A');

      await favoriteStore.onLogin('account-b');
      await flushPromises();

      expect(wrapper.text()).toContain('账号切换后暂不可读');
      expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(false);
      expect(wrapper.text()).not.toContain('曲目数');
      expect(playAll).not.toHaveBeenCalled();
    } finally {
      favoriteStore.onLogout();
      userStore.isLoggedIn = false;
      userStore.userId = '';
    }
  });
});

describe('PlaylistView row interaction', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    vi.mocked(playAll).mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('plays the focused row with Enter or Space, starting from that track', async () => {
    mockApiGet.mockResolvedValue({ status: 1, data: { list: [trackA, trackB], total: 2 } });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-kb', playlistName: 'Playlist KB' },
    });
    await flushPromises();

    const rows = wrapper.findAll('.song-row[role="button"]');
    expect(rows).toHaveLength(2);
    expect(rows[1]!.attributes('tabindex')).toBe('0');
    expect(rows[1]!.attributes('aria-label')).toContain('Song B');

    await rows[1]!.trigger('keydown.enter');
    await flushPromises();
    expect(playAll).toHaveBeenLastCalledWith([expect.objectContaining({ FileHash: 'hash-a' }), expect.objectContaining({ FileHash: 'hash-b' })], 1);

    await rows[0]!.trigger('keydown.space');
    await flushPromises();
    expect(playAll).toHaveBeenLastCalledWith([expect.objectContaining({ FileHash: 'hash-a' }), expect.objectContaining({ FileHash: 'hash-b' })], 0);
  });

  it('starts the exact clicked occurrence when a playlist repeats the same hash', async () => {
    const first = { ...trackA, SongName: 'First occurrence' };
    const second = { ...trackA, SongName: 'Second occurrence' };
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { list: [first, second], total: 2 },
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-duplicates', playlistName: 'Duplicates' },
    });
    await flushPromises();

    const rows = wrapper.findAll('.song-row[role="button"]');
    expect(rows).toHaveLength(2);
    await rows[1]!.trigger('click');
    await flushPromises();

    expect(playAll).toHaveBeenLastCalledWith(
      [
        expect.objectContaining({ SongName: 'First occurrence', FileHash: 'hash-a' }),
        expect.objectContaining({ SongName: 'Second occurrence', FileHash: 'hash-a' }),
      ],
      1,
    );
  });

  it('offers a retry that reloads the playlist after a failed request', async () => {
    let attempt = 0;
    mockApiGet.mockImplementation((path: string) => {
      if (path !== '/playlist/track/all') return Promise.resolve({ status: 1, data: { info: [] } });
      attempt += 1;
      return attempt === 1
        ? Promise.resolve({ status: 0, error: '歌单暂时不可用' })
        : Promise.resolve({ status: 1, data: { list: [trackA], total: 1 } });
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'pl-retry', playlistName: 'Playlist Retry' },
    });
    await flushPromises();

    expect(wrapper.text()).toContain('歌单暂时不可用');
    const retry = wrapper.get('[data-test="playlist-load-retry"]');
    await retry.trigger('click');
    await flushPromises();

    expect(wrapper.text()).toContain('Song A');
    expect(wrapper.find('[data-test="playlist-load-retry"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="playlist-play-all"]').exists()).toBe(true);
  });
});

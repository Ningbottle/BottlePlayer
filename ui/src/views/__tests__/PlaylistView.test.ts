import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';
import {
  __resetFavoriteMarkersForTests,
  isFavoriteMarker,
} from '../../api/favoriteMarkers';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../../platform/tauri/nativeClient')>();
  return {
    ...actual,
    apiGet: (...args: any[]) => mockApiGet(...args),
  };
});
vi.mock('../../api/playerStore', () => ({
  playAll: vi.fn(),
  playerStore: { currentTrack: null },
}));

import PlaylistView from '../PlaylistView.vue';

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
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
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

  it('marks tracks from 我喜欢的音乐 so the player heart can light up', async () => {
    __resetFavoriteMarkersForTests();
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { list: [trackA, trackB], total: 2 },
    });

    wrapper = mount(PlaylistView, {
      props: { playlistId: 'liked-1', playlistName: '我喜欢的音乐' },
    });
    await flushPromises();

    expect(isFavoriteMarker('hash-a')).toBe(true);
    expect(isFavoriteMarker('hash-b')).toBe(true);
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

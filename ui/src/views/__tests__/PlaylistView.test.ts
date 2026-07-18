import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

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

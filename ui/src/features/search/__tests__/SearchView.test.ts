import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

vi.mock('../../../playback/playerStore', () => ({
  playAll: vi.fn(),
  playerStore: { currentTrack: null },
}));

import SearchView from '../SearchView.vue';
import { playAll } from '../../../playback/index';
import { AddToPlaylistModal } from '../../library';

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

describe('SearchView skin header', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: { lists: [], total: 0 } });
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('uses SkinPageHeader instead of legacy page-head', async () => {
    wrapper = mount(SearchView, { props: { query: 'test' } });
    await flushPromises();

    expect(wrapper.find('.page-head').exists()).toBe(false);
    expect(wrapper.find('.skin-page-header').exists()).toBe(true);
    expect(wrapper.find('.skin-page-header-title').text()).toContain('搜索');
    expect(wrapper.find('.skin-page-header-kicker').text()).toMatch(/SEARCH/i);
  });
});

describe('SearchView request generation', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('ignores a stale search response after a newer query resolves', async () => {
    const a = deferred<{ status: number; data: { lists: typeof trackA[]; total: number } }>();
    const b = deferred<{ status: number; data: { lists: typeof trackB[]; total: number } }>();

    mockApiGet
      .mockImplementationOnce(() => a.promise)
      .mockImplementationOnce(() => b.promise);

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await Promise.resolve();

    await wrapper.setProps({ query: 'beta' });
    await Promise.resolve();

    // B resolves first
    b.resolve({ status: 1, data: { lists: [trackB], total: 1 } });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('Song A');
    expect(wrapper.find('.spinner').exists()).toBe(false);

    // Stale A must not overwrite B
    a.resolve({ status: 1, data: { lists: [trackA], total: 1 } });
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('Song A');
    expect(wrapper.find('.spinner').exists()).toBe(false);
  });

  it('ignores a stale search error after a newer query succeeds', async () => {
    const a = deferred<{ status: number; data: { lists: typeof trackA[]; total: number } }>();
    const b = deferred<{ status: number; data: { lists: typeof trackB[]; total: number } }>();

    mockApiGet
      .mockImplementationOnce(() => a.promise)
      .mockImplementationOnce(() => b.promise);

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await Promise.resolve();

    await wrapper.setProps({ query: 'beta' });
    await Promise.resolve();

    b.resolve({ status: 1, data: { lists: [trackB], total: 1 } });
    await flushPromises();

    a.reject(new Error('network down for alpha'));
    await flushPromises();

    expect(wrapper.text()).toContain('Song B');
    expect(wrapper.text()).not.toContain('连接 C++ 后端 Sidecar 出错');
    expect(wrapper.find('.spinner').exists()).toBe(false);
  });

  it('does not double-fetch when query changes while page > 1', async () => {
    // Mount → Next → change query. Regression: page=1 + performSearch() both fire.
    const pageful = Array.from({ length: 25 }, (_, i) => ({
      ...trackA,
      FileHash: `hash-${i}`,
      SongName: `Song ${i}`,
    }));
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { lists: pageful, total: 100 },
    });

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await flushPromises();

    const next = wrapper.findAll('button').find((b) => /Next/i.test(b.text()));
    expect(next).toBeTruthy();
    await next!.trigger('click');
    await flushPromises();

    mockApiGet.mockClear();
    await wrapper.setProps({ query: 'beta' });
    await flushPromises();

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(mockApiGet.mock.calls[0][0]).toBe('/search');
    expect(mockApiGet.mock.calls[0][1]).toMatchObject({ keywords: 'beta', page: 1 });
  });
});

describe('SearchView row interaction', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockApiGet.mockReset();
    vi.mocked(playAll).mockReset();
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('uses native playback buttons and starts from the selected track', async () => {
    mockApiGet.mockResolvedValue({ status: 1, data: { lists: [trackA, trackB], total: 2 } });

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await flushPromises();

    const playButtons = wrapper.findAll('[data-test="search-play-row"]');
    expect(playButtons).toHaveLength(2);
    expect(playButtons[1]!.element.tagName).toBe('BUTTON');
    expect(playButtons[1]!.attributes('aria-label')).toContain('Song B');

    await playButtons[1]!.trigger('click');
    expect(playAll).toHaveBeenLastCalledWith([expect.objectContaining({ FileHash: 'hash-a' }), expect.objectContaining({ FileHash: 'hash-b' })], 1);

    await playButtons[0]!.trigger('click');
    expect(playAll).toHaveBeenLastCalledWith([expect.objectContaining({ FileHash: 'hash-a' }), expect.objectContaining({ FileHash: 'hash-b' })], 0);
  });

  it('keeps the favourite control outside the playback button and does not play on activation', async () => {
    mockApiGet.mockResolvedValue({ status: 1, data: { lists: [trackA], total: 1 } });

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await flushPromises();

    const playButton = wrapper.get('[data-test="search-play-row"]');
    const favouriteButton = wrapper.get('.fav-btn');
    expect(playButton.element.contains(favouriteButton.element)).toBe(false);
    expect(favouriteButton.element.tagName).toBe('BUTTON');
    expect(favouriteButton.attributes('aria-label')).toContain('Song A');

    await favouriteButton.trigger('click');
    expect(playAll).not.toHaveBeenCalled();
    const modal = wrapper.findComponent(AddToPlaylistModal);
    expect(modal.props('show')).toBe(true);
    expect(modal.props('track')).toMatchObject({ FileHash: 'hash-a' });
  });

  it('offers a retry that reruns the search after a failed request', async () => {
    let attempt = 0;
    mockApiGet.mockImplementation(() => {
      attempt += 1;
      return attempt === 1
        ? Promise.resolve({ status: 0, error: '检索失败，服务未响应' })
        : Promise.resolve({ status: 1, data: { lists: [trackA], total: 1 } });
    });

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await flushPromises();

    expect(wrapper.text()).toContain('检索失败，服务未响应');
    await wrapper.get('[data-test="search-retry"]').trigger('click');
    await flushPromises();

    expect(wrapper.text()).toContain('Song A');
    expect(wrapper.find('[data-test="search-retry"]').exists()).toBe(false);
  });

  it('does not keep advertising the previous query result count', async () => {
    const pending = deferred<{ status: number; data: { lists: typeof trackB[]; total: number } }>();
    mockApiGet
      .mockImplementationOnce(() => Promise.resolve({ status: 1, data: { lists: [trackA], total: 37 } }))
      .mockImplementationOnce(() => pending.promise);

    wrapper = mount(SearchView, { props: { query: 'alpha' } });
    await flushPromises();
    expect(wrapper.text()).toContain('找到大约');
    expect(wrapper.text()).toContain('37');

    await wrapper.setProps({ query: 'beta' });
    await Promise.resolve();
    // The 37 belongs to 'alpha'; it must not be shown for the query in flight.
    expect(wrapper.text()).not.toContain('找到大约');

    pending.resolve({ status: 1, data: { lists: [trackB], total: 4 } });
    await flushPromises();
    expect(wrapper.text()).toContain('找到大约');
    expect(wrapper.text()).toContain('4');
  });
});

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

vi.mock('../../playback/playerStore', () => ({
  playAll: vi.fn(),
  playerStore: { currentTrack: null },
}));

import SearchView from '../SearchView.vue';

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

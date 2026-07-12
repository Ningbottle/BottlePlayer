import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));

vi.mock('../../api/playerStore', () => ({
  playAll: vi.fn(),
  playerStore: { currentTrack: null },
}));

import SearchView from '../SearchView.vue';

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

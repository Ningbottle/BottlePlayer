import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockImplementation((cmd: string) => {
    if (cmd === 'stats_get_summary')
      return Promise.resolve(
        JSON.stringify({
          total_plays: 10,
          total_listened_seconds: 3600,
          unique_songs: 5,
          unique_artists: 3,
          completion_rate: 0.8,
        }),
      );
    if (cmd === 'stats_get_top')
      return Promise.resolve(
        JSON.stringify({
          items: [
            {
              name: 'Test Song',
              singer: 'Test Artist',
              album: 'Test Album',
              cover_url: '',
              play_count: 5,
              total_listened_seconds: 300,
            },
          ],
        }),
      );
    if (cmd === 'stats_get_timeline')
      return Promise.resolve(
        JSON.stringify({ items: [{ date: '2026-06-24', count: 3 }] }),
      );
    if (cmd === 'stats_get_recent')
      return Promise.resolve(
        JSON.stringify({
          items: [
            {
              name: 'Recent Song',
              singer: 'Artist',
              album: 'Album',
              cover_url: '',
              duration_seconds: 240,
              completed: true,
              listened_seconds: 240,
              quality: '320',
              played_at: Date.now(),
            },
          ],
        }),
      );
    return Promise.resolve('{}');
  }),
}));

import StatsView from '../StatsView.vue';
import { invoke } from '@tauri-apps/api/core';

describe('StatsView data loading', () => {
  beforeEach(() => vi.clearAllMocks());

  it('mock stats_get_summary returns expected shape', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_summary', { range: '30d' })) as string,
    );
    expect(result.total_plays).toBe(10);
    expect(result.completion_rate).toBe(0.8);
  });

  it('mock stats_get_top returns items array', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_top', {
        dim: 'song',
        range: '30d',
        limit: 10,
      })) as string,
    );
    expect(result.items).toHaveLength(1);
    expect(result.items[0].name).toBe('Test Song');
  });

  it('mock stats_get_recent returns items with played_at', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_recent', { limit: 20, offset: 0 })) as string,
    );
    expect(result.items[0].name).toBe('Recent Song');
    expect(result.items[0].completed).toBe(true);
  });
});

describe('StatsView component rendering', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders overview cards after loading', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).toContain('10');
    expect(wrapper.text()).toContain('5');
    expect(wrapper.text()).toContain('80%');
  });

  it('renders top song from mocked data', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).toContain('Test Song');
    expect(wrapper.text()).toContain('Test Artist');
  });

  it('renders recent play entry', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).toContain('Recent Song');
    expect(wrapper.text()).toContain('听完');
  });

  it('renders timeline bar', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).toContain('播放时间线');
    expect(wrapper.text()).toContain('06-24');
  });
});

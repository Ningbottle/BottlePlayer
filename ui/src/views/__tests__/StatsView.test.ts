import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

const playAllMock = vi.hoisted(() => vi.fn());

vi.mock('../../api/playerStore', () => ({
  playAll: playAllMock,
  playerStore: {
    currentTrack: null,
  },
}));

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockImplementation((cmd: string, args?: Record<string, unknown>) => {
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
    if (cmd === 'stats_get_top' && args?.kind === 'song')
      return Promise.resolve(
        JSON.stringify({
          items: [
            {
              song_hash: 'hash-top-song',
              name: 'Test Song',
              singer: 'Test Artist',
              album: 'Test Album',
              cover_url: 'http://img.example/top-song.jpg',
              play_count: 5,
              total_listened_seconds: 300,
            },
          ],
        }),
      );
    if (cmd === 'stats_get_top' && args?.kind === 'artist')
      return Promise.resolve(
        JSON.stringify({
          items: [
            {
              name: 'Test Artist',
              cover_url: 'http://img.example/artist.jpg',
              play_count: 5,
              total_listened_seconds: 300,
            },
          ],
        }),
      );
    if (cmd === 'stats_get_top' && args?.kind === 'album')
      return Promise.resolve(
        JSON.stringify({
          items: [
            {
              album_id: 'album-1',
              name: 'Test Album',
              singer: 'Test Artist',
              cover_url: 'http://img.example/album.jpg',
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
              song_hash: 'hash-recent-song',
              name: 'Recent Song',
              singer: 'Artist',
              album: 'Album',
              cover_url: 'http://img.example/recent.jpg',
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
import { playAll } from '../../api/playerStore';

describe('StatsView data loading', () => {
  beforeEach(() => vi.clearAllMocks());

  it('mock stats_get_summary returns expected shape', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_summary', { range: '30d' })) as string,
    );
    expect(result.total_plays).toBe(10);
    expect(result.completion_rate).toBe(0.8);
  });

  it('mock stats_get_top returns items array for kind=song', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_top', {
        kind: 'song',
        range: '30d',
        limit: 10,
      })) as string,
    );
    expect(result.items).toHaveLength(1);
    expect(result.items[0].name).toBe('Test Song');
    expect(result.items[0].singer).toBe('Test Artist');
    expect(result.items[0].album).toBe('Test Album');
    expect(result.items[0].play_count).toBe(5);
  });

  it('mock stats_get_top returns items for every kind parameter', async () => {
    const expectedName = {
      song: 'Test Song',
      artist: 'Test Artist',
      album: 'Test Album',
    };
    for (const kind of ['song', 'artist', 'album'] as const) {
      const result = JSON.parse(
        (await invoke('stats_get_top', {
          kind,
          range: '30d',
          limit: 10,
        })) as string,
      );
      expect(result.items).toHaveLength(1);
      expect(result.items[0].name).toBe(expectedName[kind]);
    }
  });

  it('mock stats_get_recent returns items matching C++ GetRecent shape', async () => {
    const result = JSON.parse(
      (await invoke('stats_get_recent', { limit: 20, offset: 0 })) as string,
    );
    const item = result.items[0];
    expect(item.name).toBe('Recent Song');
    expect(item.singer).toBe('Artist');
    expect(item.album).toBe('Album');
    expect(item.cover_url).toBe('http://img.example/recent.jpg');
    expect(typeof item.duration_seconds).toBe('number');
    expect(item.completed).toBe(true);
    expect(typeof item.listened_seconds).toBe('number');
    expect(item.quality).toBe('320');
    expect(typeof item.played_at).toBe('number');
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
  beforeEach(() => {
    vi.clearAllMocks();
    playAllMock.mockClear();
  });

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

  it('renders artist avatars when stats provide a cover_url', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    const artistSection = wrapper.findAll('.top-section')[1];
    const img = artistSection.find('img.top-cover');
    expect(img.exists()).toBe(true);
    expect(img.attributes('src')).toBe('http://img.example/artist.jpg');
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

  it('plays the top song when clicked', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    await wrapper.findAll('.top-item')[0].trigger('click');

    expect(playAll).toHaveBeenCalledWith(
      [
        expect.objectContaining({
          FileHash: 'hash-top-song',
          SongName: 'Test Song',
          SingerName: 'Test Artist',
        }),
      ],
      0,
    );
  });

  it('plays a recent song when clicked', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    await wrapper.find('.recent-item').trigger('click');

    expect(playAll).toHaveBeenCalledWith(
      [
        expect.objectContaining({
          FileHash: 'hash-recent-song',
          SongName: 'Recent Song',
          SingerName: 'Artist',
        }),
      ],
      0,
    );
  });
});

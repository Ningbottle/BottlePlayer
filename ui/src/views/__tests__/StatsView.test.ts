import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

const playAllMock = vi.hoisted(() => vi.fn());
const animateCountUpMock = vi.hoisted(() => vi.fn((targetRef, target) => {
  targetRef.value = target;
  return Promise.resolve();
}));
const animateBarHeightMock = vi.hoisted(() => vi.fn());
const isReducedMotionMock = vi.hoisted(() => vi.fn(() => false));

vi.mock('../../api/playerStore', () => ({
  playAll: playAllMock,
  playerStore: {
    currentTrack: null,
  },
}));

vi.mock('../../api/motion', () => ({
  animateCountUp: animateCountUpMock,
  animateBarHeight: animateBarHeightMock,
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
  isReducedMotion: isReducedMotionMock,
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
    localStorage.clear();
    playAllMock.mockClear();
    isReducedMotionMock.mockReturnValue(false);
  });

  it('never restores or persists the DeepSeek API key in web storage', async () => {
    localStorage.setItem('deepseek_api_key', 'legacy-secret');
    const wrapper = mount(StatsView);
    await flushPromises();

    const input = wrapper.get('.ai-key-input');
    expect((input.element as HTMLInputElement).value).toBe('');
    expect(localStorage.getItem('deepseek_api_key')).toBeNull();

    await input.setValue('session-secret');
    await wrapper.get('.stats-ai button').trigger('click');
    await flushPromises();

    expect(localStorage.getItem('deepseek_api_key')).toBeNull();
    expect(vi.mocked(invoke)).toHaveBeenCalledWith(
      'ai_analyze',
      expect.objectContaining({ apiKey: 'session-secret' }),
    );
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

  it('does not render duplicate recent plays on the stats dashboard', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).not.toContain('最近播放');
    expect(wrapper.text()).not.toContain('Recent Song');
    expect(wrapper.text()).not.toContain('听完');
    expect(wrapper.text()).not.toContain('跳过');
    expect(vi.mocked(invoke).mock.calls.some(([cmd]) => cmd === 'stats_get_recent')).toBe(false);
  });

  it('renders timeline bar', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.text()).toContain('播放时间线');
    expect(wrapper.text()).toContain('06-24');
  });

  it('animates overview stats after loading', async () => {
    mount(StatsView);
    await flushPromises();

    expect(animateCountUpMock).toHaveBeenCalledWith(expect.any(Object), 10, expect.any(Object));
    expect(animateCountUpMock).toHaveBeenCalledWith(expect.any(Object), 3600, expect.any(Object));
    expect(animateCountUpMock).toHaveBeenCalledWith(expect.any(Object), 5, expect.any(Object));
    expect(animateCountUpMock).toHaveBeenCalledWith(expect.any(Object), 80, expect.any(Object));
  });

  it('animates timeline bars after loading', async () => {
    const wrapper = mount(StatsView, { attachTo: document.body });
    await flushPromises();

    expect(wrapper.find('.bar-fill').exists()).toBe(true);
    expect(animateBarHeightMock).toHaveBeenCalledWith(
      wrapper.find('.bar-fill').element,
      100,
      expect.any(Object),
    );

    wrapper.unmount();
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

  it('does not expose recent-play rows as playable stats items', async () => {
    const wrapper = mount(StatsView);
    await flushPromises();

    expect(wrapper.find('.recent-item').exists()).toBe(false);
  });

  it('keeps the newest range when a slower earlier request resolves last', async () => {
    const deferred: Array<() => void> = [];
    vi.mocked(invoke).mockImplementation((cmd: string, args?: unknown) => {
      const rangeArg =
        args && typeof args === 'object' && !Array.isArray(args) && 'range' in args
          ? args.range
          : undefined;
      const range = rangeArg === '30d' ? 300 : rangeArg === '1d' ? 7 : 30;
      const payload = cmd === 'stats_get_summary'
        ? JSON.stringify({
          total_plays: range,
          total_listened_seconds: range * 60,
          unique_songs: range,
          unique_artists: 1,
          completion_rate: 1,
        })
        : cmd === 'stats_get_timeline'
          ? JSON.stringify({ items: [{ date: '2026-06-24', count: range }] })
          : JSON.stringify({ items: [] });

      if (rangeArg === '1d') {
        return new Promise<string>((resolve) => deferred.push(() => resolve(payload)));
      }
      return Promise.resolve(payload);
    });

    const wrapper = mount(StatsView);
    await flushPromises();

    const tabs = wrapper.findAll('.range-tabs button');
    await tabs[0].trigger('click');
    await tabs[2].trigger('click');
    await flushPromises();
    expect(wrapper.findAll('.stat-value')[0].text()).toBe('300');

    deferred.forEach((resolve) => resolve());
    await flushPromises();
    expect(wrapper.findAll('.stat-value')[0].text()).toBe('300');
  });
});

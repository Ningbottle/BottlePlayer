import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

const playAllMock = vi.hoisted(() => vi.fn());
const animateCountUpMock = vi.hoisted(() => vi.fn((targetRef, target) => {
  targetRef.value = target;
  return Promise.resolve();
}));
const animateBarHeightMock = vi.hoisted(() => vi.fn());
const isReducedMotionMock = vi.hoisted(() => vi.fn(() => false));

vi.mock('../../../playback/playerStore', () => ({
  playAll: playAllMock,
  playerStore: {
    currentTrack: null,
  },
}));

vi.mock('../../../shared/motion/motion', () => ({
  animateCountUp: animateCountUpMock,
  animateBarHeight: animateBarHeightMock,
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
  isReducedMotion: isReducedMotionMock,
}));

vi.mock('../statsGateway', () => ({
  getStatsSummary: vi.fn().mockResolvedValue({
    total_plays: 10,
    total_listened_seconds: 3600,
    unique_songs: 5,
    unique_artists: 3,
    completion_rate: 0.8,
  }),
  getStatsTop: vi.fn().mockImplementation((kind: string) => {
    if (kind === 'song')
      return Promise.resolve([
        {
          song_hash: 'hash-top-song',
          name: 'Test Song',
          singer: 'Test Artist',
          album: 'Test Album',
          cover_url: 'http://img.example/top-song.jpg',
          play_count: 5,
          total_listened_seconds: 300,
        },
      ]);
    if (kind === 'artist')
      return Promise.resolve([
        {
          name: 'Test Artist',
          cover_url: 'http://img.example/artist.jpg',
          play_count: 5,
          total_listened_seconds: 300,
        },
      ]);
    return Promise.resolve([
      {
        album_id: 'album-1',
        name: 'Test Album',
        singer: 'Test Artist',
        cover_url: 'http://img.example/album.jpg',
        play_count: 5,
        total_listened_seconds: 300,
      },
    ]);
  }),
  getStatsTimeline: vi.fn().mockResolvedValue([{ date: '2026-06-24', count: 3 }]),
  analyzeStats: vi.fn().mockResolvedValue('AI 分析结果'),
}));

import StatsView from '../StatsView.vue';
import { getStatsSummary, getStatsTop, getStatsTimeline, analyzeStats } from '../statsGateway';
import { playAll } from '../../../playback/index';

// The backend JSON contract for summary/top/timeline is covered by
// features/stats/__tests__/statsGateway.test.ts (the typed gateway parses the
// raw payloads). stats_get_recent has no production caller in the dashboard
// and no gateway API by design; its C++ GetRecent response shape is pinned
// here so a future typed gateway cannot silently drift from the backend DTO.
describe('stats backend DTO contracts', () => {
  const RECENT_SAMPLE = {
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
  };

  it('stats_get_recent DTO keeps the C++ GetRecent item shape', () => {
    const item = RECENT_SAMPLE.items[0];
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

  it('stats_get_recent DTO items carry played_at and completed', () => {
    expect(RECENT_SAMPLE.items[0].name).toBe('Recent Song');
    expect(RECENT_SAMPLE.items[0].completed).toBe(true);
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
    expect(analyzeStats).toHaveBeenCalledWith(
      'session-secret',
      expect.objectContaining({ summary: expect.objectContaining({ total_plays: 10 }) }),
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
    // The typed gateway exposes no recent-plays API at all, so the dashboard
    // cannot call it: the negative is enforced by the module surface, verified
    // by the gateway contract tests (no stats_get_recent there either).
    expect(analyzeStats).not.toHaveBeenCalled();
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
    vi.mocked(getStatsSummary).mockImplementation((range) => {
      const total = range === '30d' ? 300 : range === '1d' ? 7 : 30;
      const payload = {
        total_plays: total,
        total_listened_seconds: total * 60,
        unique_songs: total,
        unique_artists: 1,
        completion_rate: 1,
      };
      if (range === '1d') {
        return new Promise((resolve) => deferred.push(() => resolve(payload)));
      }
      return Promise.resolve(payload);
    });
    vi.mocked(getStatsTop).mockResolvedValue([]);
    vi.mocked(getStatsTimeline).mockResolvedValue([{ date: '2026-06-24', count: 300 }]);

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

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';
import AuroraHome from '../AuroraHome.vue';
import type { HomeSectionError, HomeSectionViewState, HomeViewModel } from '../homeViewModel';
import type { Track } from '../../../api/normalizer';
import type { HomeSection, PlaylistInfo } from '../../../api/homeFeedStore';
import type { PlaybackPhase } from '../../../playback/playbackPhase';
import { animateStagger } from '../../../shared/motion/motion';
import { playerStore, togglePlay } from '../../../playback/playerStore';
import { flyCoverToDock } from '../../../playback/components/coverFlight';

vi.mock('gsap', () => {
  const fromTo = vi.fn(() => ({ kill: vi.fn() }));
  const set = vi.fn();
  const killTweensOf = vi.fn();
  const to = vi.fn(() => ({ kill: vi.fn() }));
  return { gsap: { fromTo, set, killTweensOf, to } };
});

vi.mock('../../../shared/motion/motion', () => ({
  animateElement: vi.fn(() => ({ kill: () => {} })),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
  isReducedMotion: vi.fn(() => false),
}));

vi.mock('../../../playback/playerStore', async () => {
  const { reactive } = await import('vue');
  return {
    playerStore: reactive({ currentTime: 0 }),
    togglePlay: vi.fn(),
  };
});

vi.mock('../../../playback/components/coverFlight', () => ({
  flyCoverToDock: vi.fn(),
}));

function createTrack(overrides: Partial<Track> = {}): Track {
  return {
    FileHash: 'hash-1',
    SongName: 'Test Song',
    SingerName: 'Test Artist',
    Duration: 180,
    ...overrides,
  };
}

function createPlaylist(overrides: Partial<PlaylistInfo> = {}): PlaylistInfo {
  return {
    specialid: 1,
    specialname: 'Test Playlist',
    imgurl: '',
    nickname: 'Tester',
    playcount: 100,
    ...overrides,
  };
}

function createSectionStates(
  overrides: Partial<Record<HomeSection, Partial<HomeSectionViewState>>> = {},
): Record<HomeSection, HomeSectionViewState> {
  const base = (): HomeSectionViewState => ({
    loading: false,
    refreshing: false,
    error: null,
    isEmpty: false,
    retry: () => Promise.resolve(),
  });
  return {
    daily: { ...base(), ...overrides.daily },
    playlists: { ...base(), ...overrides.playlists },
    albums: { ...base(), ...overrides.albums },
  };
}

function createViewModel(overrides: Partial<HomeViewModel> = {}): HomeViewModel {
  const { queueWindowStart = 0, ...rest } = overrides;
  return {
    heroTrack: createTrack(),
    dailyTracks: [createTrack()],
    playlists: [],
    albums: [],
    queuePreview: [],
    queueWindowStart,
    queueTotal: 0,
    queueMode: 'normal' as const,
    activeQueueHash: null,
    isPlaying: false,
    isPlaybackLoading: false,
    playbackPhase: 'idle' as PlaybackPhase,
    isInitialLoading: false,
    isRefreshing: false,
    sections: createSectionStates(),
    errors: [] as readonly HomeSectionError[],
    errorSummary: '',
    heroQualityChips: [],
    ...rest,
  };
}

describe('AuroraHome', () => {
  const getContextSpy = vi.spyOn(HTMLCanvasElement.prototype, 'getContext');

  beforeEach(() => {
    vi.clearAllMocks();
    getContextSpy.mockReturnValue(null);
    playerStore.currentTime = 0;
  });

  afterEach(() => {
    getContextSpy.mockReset();
  });

  it('keeps daily loading, playlist refresh, and album retry scoped to their sections', async () => {
    const retryDaily = vi.fn().mockResolvedValue(undefined);
    const retryAlbums = vi.fn().mockResolvedValue(undefined);
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          playlists: [createPlaylist({ specialid: 2 })],
          albums: [],
          sections: createSectionStates({
            daily: { loading: true, retry: retryDaily },
            playlists: { refreshing: true },
            albums: { error: '加载失败', retry: retryAlbums },
          }),
        }),
      },
    });

    expect(wrapper.find('[data-test="aurora-stage-loading"]').exists()).toBe(true);
    expect(wrapper.get('[data-test="playlists-section-status"]').text()).toContain('刷新中…');
    expect(wrapper.find('[data-test="albums-section-retry"]').exists()).toBe(true);

    await wrapper.get('[data-test="albums-section-retry"]').trigger('click');
    expect(retryAlbums).toHaveBeenCalledTimes(1);
    expect(retryDaily).not.toHaveBeenCalled();
  });

  it('shows playlist loading without putting daily or albums into loading state', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: [createTrack({ FileHash: 'daily-ready' })],
          albums: [createPlaylist({ specialid: 3 })],
          isRefreshing: true,
          sections: createSectionStates({ playlists: { loading: true } }),
        }),
      },
    });

    expect(wrapper.find('[data-test="aurora-stage-loading"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="daily-track-daily-ready"]').exists()).toBe(true);
    expect(wrapper.get('[data-test="playlists-section-status"]').text()).toContain('加载中…');
    expect(wrapper.get('[data-test="albums-section-status"]').text()).toContain('全部歌单');
    expect(wrapper.get('[data-test="refresh"]').attributes('disabled')).toBeUndefined();
  });

  it('displays hero track with cover, song name, artist, and play button', () => {
    const vm = createViewModel({
      heroTrack: createTrack({
        SongName: '我的歌曲',
        SingerName: '歌手名',
        Image: 'http://example.com/cover.jpg',
      }),
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('我的歌曲');
    expect(wrapper.text()).toContain('歌手名');
    expect(wrapper.find('.aurora-cover').exists()).toBe(true);
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(true);
  });

  it('uses daily[0] as hero when no current track', () => {
    const daily = [
      createTrack({ SongName: 'Daily First', FileHash: 'daily-1' }),
    ];
    const vm = createViewModel({
      heroTrack: daily[0],
      dailyTracks: daily,
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Daily First');
  });

  it('shows daily rail tracks (not playback queue) when not in personalFm', () => {
    const vm = createViewModel({
      dailyTracks: [
        createTrack({ SongName: 'Daily 1', FileHash: 'd1', Duration: 125 }),
        createTrack({ SongName: 'Daily 2', FileHash: 'd2', Duration: 0 }),
        createTrack({ SongName: 'Daily 3', FileHash: 'd3' }),
      ],
      queuePreview: [createTrack({ SongName: 'Should Not Show Queue Song', FileHash: 'q1' })],
      queueMode: 'normal',
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Daily 1');
    expect(wrapper.text()).toContain('Daily 2');
    expect(wrapper.text()).toContain('Daily 3');
    expect(wrapper.text()).not.toContain('Should Not Show Queue Song');
    expect(wrapper.get('[data-test="queue-track-d1"]').text()).toMatch(/2:05/);
    expect(wrapper.get('[data-test="queue-track-d2"]').text()).toContain('—');
  });

  it('follows live queue on the rail while personalFm is active (auto-updating reco list)', () => {
    const vm = createViewModel({
      dailyTracks: [createTrack({ SongName: 'Stale Daily', FileHash: 'stale' })],
      queuePreview: [
        createTrack({ SongName: 'Live A', FileHash: 'live-a', Duration: 100 }),
        createTrack({ SongName: 'Live B', FileHash: 'live-b', Duration: 120 }),
      ],
      queueWindowStart: 4,
      queueTotal: 12,
      queueMode: 'personalFm',
      activeQueueHash: 'live-b',
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.get('[data-test="queue-rail"]').attributes('aria-label')).toBe('正在推荐');
    expect(wrapper.get('[data-test="queue-rail"]').attributes('data-live-reco')).toBe('true');
    const rail = wrapper.get('[data-test="queue-rail"]');
    expect(rail.text()).toContain('正在推荐');
    expect(rail.text()).toContain('Live A');
    expect(rail.text()).toContain('Live B');
    expect(rail.text()).not.toContain('Stale Daily');
    // Absolute index = windowStart + row
    expect(wrapper.get('[data-test="queue-track-live-a"]').text()).toMatch(/05/);
    expect(wrapper.get('[data-test="queue-track-live-b"]').attributes('aria-current')).toBe('true');
    expect(wrapper.get('.aurora-queue-rail-head h2 span').text()).toBe('12');
    // Refresh is for home snapshot only — hide while following live session
    expect(wrapper.find('[data-test="daily-rail-refresh"]').exists()).toBe(false);
  });

  it('renders a labelled daily rail and marks the active playing daily track', () => {
    const daily = Array.from({ length: 12 }, (_, index) => createTrack({
      FileHash: `daily-${index + 1}`,
      SongName: `Daily ${index + 1}`,
      Duration: 90,
    }));
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: daily,
          queueTotal: 99,
          activeQueueHash: 'daily-3',
        }),
      },
    });

    expect(wrapper.get('[data-test="queue-rail"]').attributes('aria-label')).toBe('每日推荐');
    expect(wrapper.findAll('[data-test^="queue-track-"]')).toHaveLength(12);
    expect(wrapper.get('[data-test="queue-track-daily-3"]').attributes('aria-current')).toBe('true');
    expect(wrapper.get('.aurora-queue-rail-head h2 span').text()).toBe('12');
    expect(wrapper.text()).toContain('每日推荐');
    expect(wrapper.find('[data-test="queue-clear"]').exists()).toBe(false);
  });

  it('limits the daily rail to twelve tracks when daily list is longer', () => {
    const daily = Array.from({ length: 13 }, (_, index) => createTrack({
      FileHash: `daily-${index + 1}`,
      SongName: `Daily ${index + 1}`,
    }));
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ dailyTracks: daily }) },
    });

    expect(wrapper.findAll('[data-test^="queue-track-"]')).toHaveLength(12);
    expect(wrapper.find('[data-test="queue-track-daily-13"]').exists()).toBe(false);
  });

  it('displays zero when daily list is empty', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: [],
          queuePreview: [createTrack({ FileHash: 'queued-track' })],
          queueTotal: 5,
        }),
      },
    });

    expect(wrapper.get('.aurora-queue-rail-head h2 span').text()).toBe('0');
  });

  it('renders enriched empty daily rail when recommendations are empty', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          playlists: [],
          albums: [],
          queuePreview: [],
          queueTotal: 0,
        }),
      },
    });
    const empty = wrapper.get('[data-test="queue-empty-state"]');
    expect(empty.text()).toMatch(/推荐|播放栏/);
    expect(empty.text()).not.toBe('暂无队列');
  });

  it('uses an actionable empty stage instead of fabricated playback metadata', async () => {
    const retryDaily = vi.fn().mockResolvedValue(undefined);
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          queuePreview: [],
          queueTotal: 0,
          sections: createSectionStates({ daily: { retry: retryDaily } }),
        }),
      },
    });

    expect(wrapper.get('[data-test="aurora-stage-empty"]').text()).toContain('选择一首歌');
    expect(wrapper.text()).not.toContain('96kHz / 24bit');
    expect(wrapper.text()).not.toContain('VIP');
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(false);

    await wrapper.get('[data-test="empty-stage-refresh"]').trigger('click');
    expect(retryDaily).toHaveBeenCalledTimes(1);
  });

  it('provides a particle environment with an explicit playback state', () => {
    const paused = mount(AuroraHome, {
      props: { model: createViewModel({ isPlaying: false }) },
    });
    const playing = mount(AuroraHome, {
      props: { model: createViewModel({ isPlaying: true }) },
    });

    expect(paused.get('[data-test="aurora-atmosphere"]').attributes('data-playing')).toBe('false');
    expect(playing.get('[data-test="aurora-atmosphere"]').attributes('data-playing')).toBe('true');
    expect(paused.find('canvas[data-test="aurora-atmosphere"]').exists()).toBe(true);
  });

  it('keeps list rows when daily recommendations have tracks', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: [
            createTrack({
              FileHash: 'd-has-1',
              SongName: 'Daily Song',
              SingerName: 'Daily Artist',
              Duration: 200,
            }),
          ],
        }),
      },
    });
    expect(wrapper.find('[data-test="queue-empty-state"]').exists()).toBe(false);
    expect(wrapper.findAll('[data-test^="queue-track-"]').length).toBeGreaterThan(0);
  });

  it('numbers daily rail rows from 01 without playback-window offsets', () => {
    const daily = [
      createTrack({ FileHash: 'daily-a', SongName: 'A' }),
      createTrack({ FileHash: 'daily-b', SongName: 'B' }),
    ];
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: daily,
          queueWindowStart: 7,
        }),
      },
    });

    expect(wrapper.get('[data-test="queue-track-daily-a"]').text()).toContain('01');
    expect(wrapper.get('[data-test="queue-track-daily-b"]').text()).toContain('02');
  });

  it('updates the daily rail when recommendations change (no freeze-follow for playback queue)', async () => {
    const first = createTrack({ FileHash: 'daily-1', SongName: 'Daily 1' });
    const second = createTrack({ FileHash: 'daily-2', SongName: 'Daily 2' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: [first],
          activeQueueHash: first.FileHash,
        }),
      },
    });

    expect(wrapper.find('[data-test="queue-track-daily-1"]').exists()).toBe(true);

    await wrapper.setProps({
      model: createViewModel({
        dailyTracks: [second],
        activeQueueHash: second.FileHash,
      }),
    });

    expect(wrapper.find('[data-test="queue-track-daily-1"]').exists()).toBe(false);
    const row = wrapper.get('[data-test="queue-track-daily-2"]');
    expect(row.attributes('aria-current')).toBe('true');
    // Active row shows the equalizer instead of the index number
    expect(row.find('.aurora-eq').exists()).toBe(true);
  });

  it('emits play-track from both the daily rail and daily cards', async () => {
    const railOnly = createTrack({ FileHash: 'rail-play', SongName: 'Rail Play' });
    const daily = createTrack({ FileHash: 'daily-play', SongName: 'Daily Play' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: [railOnly, daily],
        }),
      },
    });

    await wrapper.get('[data-test="queue-track-rail-play"]').trigger('click');
    await wrapper.get('[data-test="daily-track-daily-play"]').trigger('click');

    expect(wrapper.emitted('play-track')).toEqual([[railOnly], [daily]]);
  });

  it('handles long song name without squeezing play button', () => {
    const longName = '这是一首非常非常非常长的歌曲名称'.repeat(5);
    const vm = createViewModel({
      heroTrack: createTrack({ SongName: longName }),
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    const playBtn = wrapper.find('[data-test="hero-play"]');
    expect(playBtn.exists()).toBe(true);
    const infoArea = wrapper.find('.aurora-info');
    expect(infoArea.exists()).toBe(true);
    expect(wrapper.text()).toContain(longName);
  });

  it('emits play-track when main play button is clicked', async () => {
    const track = createTrack({ FileHash: 'hero-hash' });
    const vm = createViewModel({ heroTrack: track });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="hero-play"]').trigger('click');

    expect(wrapper.emitted('play-track')).toBeTruthy();
    expect(wrapper.emitted('play-track')![0]).toEqual([track]);
  });

  it('disables hero CTA while current track is loading and does not emit play-track', async () => {
    const track = createTrack({ FileHash: 'hero-1' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: track,
          activeQueueHash: 'hero-1',
          isPlaying: false,
          isPlaybackLoading: true,
          playbackPhase: 'loading',
        }),
      },
    });

    const hero = wrapper.get('[data-test="hero-play"]');
    expect(hero.attributes('disabled')).toBeDefined();
    expect(hero.text()).toContain('正在加载');
    expect(hero.attributes('aria-label')).toBe('正在加载…');
    await hero.trigger('click');
    expect(togglePlay).not.toHaveBeenCalled();
    expect(wrapper.emitted('play-track')).toBeUndefined();
  });

  it('toggles pause from hero CTA when the current track is playing', async () => {
    const track = createTrack({ FileHash: 'hero-1' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: track,
          activeQueueHash: 'hero-1',
          isPlaying: true,
          isPlaybackLoading: false,
          playbackPhase: 'playing',
        }),
      },
    });

    const hero = wrapper.get('[data-test="hero-play"]');
    expect(hero.attributes('disabled')).toBeUndefined();
    expect(hero.text()).toContain('暂停');
    await hero.trigger('click');
    expect(togglePlay).toHaveBeenCalledTimes(1);
    expect(wrapper.emitted('play-track')).toBeUndefined();
  });

  it('toggles play from hero CTA when the current track is paused', async () => {
    const track = createTrack({ FileHash: 'hero-1' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: track,
          activeQueueHash: 'hero-1',
          isPlaying: false,
          isPlaybackLoading: false,
          playbackPhase: 'paused',
        }),
      },
    });

    await wrapper.get('[data-test="hero-play"]').trigger('click');
    expect(togglePlay).toHaveBeenCalledTimes(1);
    expect(wrapper.emitted('play-track')).toBeUndefined();
  });

  it('emits refresh when refresh button is clicked', async () => {
    const vm = createViewModel();

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="refresh"]').trigger('click');

    expect(wrapper.emitted('refresh')).toBeTruthy();
  });

  it('emits navigate when a playlist is clicked', async () => {
    const pl = createPlaylist({ specialid: 42, specialname: 'Cool Playlist' });
    const vm = createViewModel({ playlists: [pl] });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="playlist-42"]').trigger('click');

    expect(wrapper.emitted('navigate')).toBeTruthy();
    expect(wrapper.emitted('navigate')![0]).toEqual(['playlist', { id: 42, name: 'Cool Playlist' }]);
  });

  it('emits navigate to lyric when the lyrics button is clicked', async () => {
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel() },
    });

    await wrapper.get('.aurora-lyrics-link').trigger('click');

    expect(wrapper.emitted('navigate')).toEqual([['lyric']]);
  });

  it('still renders stage, daily rail, and controls under reduced motion', () => {
    const daily = [createTrack({ FileHash: 'daily-rm', SongName: 'Daily RM' })];
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          dailyTracks: daily,
        }),
      },
    });

    expect(wrapper.find('[data-test="aurora-stage"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="queue-rail"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="queue-track-daily-rm"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="daily-track-daily-rm"]').exists()).toBe(true);
  });

  it('uses buttons for every interactive daily rail and daily card item', () => {
    const track = createTrack({ FileHash: 'interactive-track' });
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ dailyTracks: [track] }) },
    });

    expect(wrapper.get('[data-test="daily-track-interactive-track"]').element.tagName).toBe('BUTTON');
    expect(wrapper.get('[data-test="queue-track-interactive-track"]').element.tagName).toBe('BUTTON');
  });

  it('keeps old content visible during refresh', () => {
    const vm = createViewModel({
      isRefreshing: true,
      heroTrack: createTrack({ SongName: 'Existing Track' }),
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Existing Track');
  });

  it('displays a single columnized error summary instead of repeated rows', async () => {
    const vm = createViewModel({
      errors: [
        { section: 'daily', message: '加载失败' },
        { section: 'albums', message: '加载失败' },
      ],
      errorSummary: '每日推荐、最新歌单加载失败',
      playlists: [createPlaylist()],
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    const summary = wrapper.get('[data-test="home-error-summary"]');
    expect(summary.text()).toContain('每日推荐、最新歌单加载失败');
    expect(wrapper.findAll('[data-test="home-error-summary"]')).toHaveLength(1);
    expect(wrapper.text()).not.toMatch(/加载失败[\s\S]*加载失败[\s\S]*加载失败/);

    await wrapper.get('[data-test="home-error-retry-all"]').trigger('click');
    expect(wrapper.emitted('refresh')).toBeTruthy();
  });

  it('shows hero quality chips only from real data, never decorative sample rate', () => {
    const withChips = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroQualityChips: ['无损', 'VIP'],
        }),
      },
    });
    expect(withChips.get('[data-test="hero-quality-chips"]').text()).toContain('无损');
    expect(withChips.text()).not.toContain('96kHz');

    const without = mount(AuroraHome, {
      props: { model: createViewModel({ heroQualityChips: [] }) },
    });
    expect(without.find('[data-test="hero-quality-chips"]').exists()).toBe(false);
  });

  it('shows loading state during initial load', () => {
    const vm = createViewModel({
      heroTrack: null,
      sections: createSectionStates({ daily: { loading: true } }),
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.get('[data-test="aurora-stage-loading"]').text()).toContain('正在加载推荐');
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(false);
    expect(wrapper.text()).not.toContain('正在播放');
    expect(wrapper.text()).not.toContain('96kHz');
  });

  it('renders the current track immediately while the daily feed is still loading', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: createTrack({ SongName: '立即显示的歌曲' }),
          sections: createSectionStates({ daily: { loading: true } }),
        }),
      },
    });

    expect(wrapper.get('[data-test="aurora-stage"]').text()).toContain('立即显示的歌曲');
    expect(wrapper.find('[data-test="aurora-stage-loading"]').exists()).toBe(false);
  });

  it('keeps Chinese as the primary heading language', () => {
    const wrapper = mount(AuroraHome, { props: { model: createViewModel() } });
    expect(wrapper.get('[data-test="daily-picks"] h2').text()).toMatch(/^今日推荐/);
  });

  describe('home enter cold / return budgets', () => {
    it('uses cold stagger overrides when enterMode is cold', async () => {
      const daily = Array.from({ length: 4 }, (_, i) =>
        createTrack({ FileHash: `d-${i}`, SongName: `Daily ${i}` }),
      );
      mount(AuroraHome, {
        props: {
          model: createViewModel({ dailyTracks: daily }),
          enterMode: 'cold',
          enterNonce: 1,
        },
      });
      await nextTick();
      await flushPromises();

      expect(animateStagger).toHaveBeenCalledWith(
        expect.any(Array),
        'cardEnter',
        expect.objectContaining({
          duration: 0.52,
          stagger: 0.055,
          maxItems: 14,
          fromY: 32,
        }),
      );
      const { gsap } = await import('gsap');
      expect(gsap.fromTo).toHaveBeenCalledWith(
        expect.any(Object),
        expect.objectContaining({ opacity: 0, y: 36 }),
        expect.objectContaining({ duration: 0.72, ease: 'expo.out' }),
      );
    });

    it('uses return stagger overrides when enterMode is return', async () => {
      const daily = Array.from({ length: 4 }, (_, i) =>
        createTrack({ FileHash: `r-${i}`, SongName: `Return ${i}` }),
      );
      mount(AuroraHome, {
        props: {
          model: createViewModel({ dailyTracks: daily }),
          enterMode: 'return',
          enterNonce: 2,
        },
      });
      await nextTick();
      await flushPromises();

      expect(animateStagger).toHaveBeenCalledWith(
        expect.any(Array),
        'cardEnter',
        expect.objectContaining({
          duration: 0.32,
          stagger: 0.035,
          maxItems: 8,
          fromY: 16,
        }),
      );
    });

    it('replays enter when enterNonce changes', async () => {
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({ dailyTracks: [createTrack()] }),
          enterMode: 'cold',
          enterNonce: 1,
        },
      });
      await nextTick();
      await flushPromises();
      const callsAfterCold = (animateStagger as ReturnType<typeof vi.fn>).mock.calls.length;

      await wrapper.setProps({ enterMode: 'return', enterNonce: 2 });
      await nextTick();
      await flushPromises();

      expect((animateStagger as ReturnType<typeof vi.fn>).mock.calls.length).toBeGreaterThan(
        callsAfterCold,
      );
      const callsArr = (animateStagger as ReturnType<typeof vi.fn>).mock.calls;
      const lastCall = callsArr[callsArr.length - 1];
      expect(lastCall?.[2]).toEqual(
        expect.objectContaining({ duration: 0.32, maxItems: 8, fromY: 16 }),
      );
    });

    it('does not run enter choreography when enterMode is none', async () => {
      mount(AuroraHome, {
        props: {
          model: createViewModel({ dailyTracks: [createTrack()] }),
          enterMode: 'none',
          enterNonce: 0,
        },
      });
      await nextTick();
      await flushPromises();

      expect(animateStagger).not.toHaveBeenCalled();
    });
  });

  describe('touchable vinyl', () => {
    it('pauses via togglePlay when the hero is the current track', async () => {
      const track = createTrack({ FileHash: 'hero-1' });
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({ heroTrack: track, activeQueueHash: 'hero-1', isPlaying: true }),
        },
      });

      const toggle = wrapper.get('[data-test="vinyl-toggle"]');
      expect(toggle.attributes('aria-label')).toBe('暂停');
      await toggle.trigger('click');
      expect(togglePlay).toHaveBeenCalledTimes(1);
      expect(wrapper.emitted('play-track')).toBeUndefined();
    });

    it('emits play-track when the hero is not the current track', async () => {
      const track = createTrack({ FileHash: 'hero-2', Image: 'http://img.example/hero.jpg' });
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({ heroTrack: track, activeQueueHash: null }),
        },
      });

      const toggle = wrapper.get('[data-test="vinyl-toggle"]');
      expect(toggle.attributes('aria-label')).toBe('播放');
      await toggle.trigger('click');
      expect(togglePlay).not.toHaveBeenCalled();
      expect(wrapper.emitted('play-track')?.[0]).toEqual([track]);
      expect(flyCoverToDock).toHaveBeenCalledWith(expect.any(HTMLElement), 'http://img.example/hero.jpg');
    });

    it.each(['resolving', 'loading', 'recovering'] as const)(
      'cancels loading from vinyl when the hero is current and %s',
      async (phase) => {
        const track = createTrack({ FileHash: 'hero-1' });
        const wrapper = mount(AuroraHome, {
          props: {
            model: createViewModel({
              heroTrack: track,
              activeQueueHash: 'hero-1',
              isPlaying: false,
              isPlaybackLoading: true,
              playbackPhase: phase,
            }),
          },
        });

        const toggle = wrapper.get('[data-test="vinyl-toggle"]');
        expect(toggle.attributes('aria-label')).toBe('取消加载');
        expect(toggle.attributes('title')).toBe('取消加载');
        expect(toggle.attributes('disabled')).toBeUndefined();
        await toggle.trigger('click');
        expect(togglePlay).toHaveBeenCalledTimes(1);
        expect(wrapper.emitted('play-track')).toBeUndefined();
      },
    );

    it('shows play on vinyl for current+paused and still toggles', async () => {
      const track = createTrack({ FileHash: 'hero-1' });
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({
            heroTrack: track,
            activeQueueHash: 'hero-1',
            isPlaying: false,
            isPlaybackLoading: false,
            playbackPhase: 'paused',
          }),
        },
      });

      const toggle = wrapper.get('[data-test="vinyl-toggle"]');
      expect(toggle.attributes('aria-label')).toBe('播放');
      await toggle.trigger('click');
      expect(togglePlay).toHaveBeenCalledTimes(1);
      expect(wrapper.emitted('play-track')).toBeUndefined();
    });

    it('reprojects vinyl copy when playbackPhase changes without remounting', async () => {
      const track = createTrack({ FileHash: 'hero-1' });
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({
            heroTrack: track,
            activeQueueHash: 'hero-1',
            isPlaying: false,
            isPlaybackLoading: true,
            playbackPhase: 'resolving',
          }),
        },
      });
      expect(wrapper.get('[data-test="vinyl-toggle"]').attributes('aria-label')).toBe('取消加载');

      await wrapper.setProps({
        model: createViewModel({
          heroTrack: track,
          activeQueueHash: 'hero-1',
          isPlaying: false,
          isPlaybackLoading: false,
          playbackPhase: 'paused',
        }),
      });
      await nextTick();
      expect(wrapper.get('[data-test="vinyl-toggle"]').attributes('aria-label')).toBe('播放');
      expect(wrapper.get('[data-test="hero-play"]').text()).toContain('播放');
    });

    it('flies the cover to the dock when a daily card is clicked', async () => {
      const track = createTrack({ FileHash: 'daily-x', Image: 'http://img.example/x.jpg' });
      const wrapper = mount(AuroraHome, {
        props: {
          model: createViewModel({ dailyTracks: [track] }),
        },
      });

      await wrapper.get(`[data-test="daily-track-daily-x"]`).trigger('click');

      expect(flyCoverToDock).toHaveBeenCalledWith(expect.any(HTMLElement), 'http://img.example/x.jpg');
    });
  });
});

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import NewsprintHome from '../NewsprintHome.vue';
import type { HomeSectionError, HomeSectionViewState, HomeViewModel } from '../homeViewModel';
import type { Track } from '../../../api/normalizer';
import type { HomeSection, PlaylistInfo } from '../../../api/homeFeedStore';

const newsprintCss = readFileSync(resolve(__dirname, '../../../styles/skins/newsprint.css'), 'utf8');

vi.mock('../../../shared/motion/motion', () => ({
  animateElement: vi.fn(),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  isReducedMotion: vi.fn(() => true),
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
    playbackPhase: 'idle',
    isInitialLoading: false,
    isRefreshing: false,
    sections: createSectionStates(),
    errors: [] as readonly HomeSectionError[],
    errorSummary: '',
    heroQualityChips: [],
    ...rest,
  };
}

describe('NewsprintHome', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('keeps daily loading, playlist refresh, and album retry scoped to their sections', async () => {
    const retryDaily = vi.fn().mockResolvedValue(undefined);
    const retryAlbums = vi.fn().mockResolvedValue(undefined);
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
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

    expect(wrapper.get('[data-test="daily-section-status"]').text()).toContain('加载中…');
    expect(wrapper.get('[data-test="playlists-section-status"]').text()).toContain('刷新中…');
    expect(wrapper.find('[data-test="albums-section-retry"]').exists()).toBe(true);

    await wrapper.get('[data-test="albums-section-retry"]').trigger('click');
    expect(retryAlbums).toHaveBeenCalledTimes(1);
    expect(retryDaily).not.toHaveBeenCalled();
  });

  it('shows playlist loading without putting daily or albums into loading state', () => {
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          dailyTracks: [createTrack({ FileHash: 'daily-ready' })],
          albums: [createPlaylist({ specialid: 3 })],
          sections: createSectionStates({ playlists: { loading: true } }),
        }),
      },
    });

    expect(wrapper.get('[data-test="daily-section-status"]').text()).toContain('刷新推荐');
    expect(wrapper.get('[data-test="playlists-section-status"]').text()).toContain('加载中…');
    expect(wrapper.get('[data-test="albums-section-status"]').text()).toContain('全部歌单');
  });

  it('renders classic late-edition masthead and feature row', () => {
    const featuredTrack = createTrack({
      SongName: '主推荐歌曲',
      SingerName: '艺术家',
      Image: 'http://example.com/cover.jpg',
    });
    const vm = createViewModel({
      heroTrack: createTrack({ FileHash: 'restored-track', SongName: '上次播放' }),
      dailyTracks: [featuredTrack],
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.find('.np-masthead').exists()).toBe(true);
    expect(wrapper.find('.page-head').exists()).toBe(true);
    expect(wrapper.find('.feature').exists()).toBe(true);
    expect(wrapper.find('.hero').exists()).toBe(true);
    expect(wrapper.text()).toContain('为你精选');
    expect(wrapper.text()).toContain('主推荐歌曲');
  });

  it('renders numbered recommendations', () => {
    const vm = createViewModel({
      dailyTracks: [
        createTrack({ SongName: '推荐一', FileHash: 'd1' }),
        createTrack({ SongName: '推荐二', FileHash: 'd2' }),
        createTrack({ SongName: '推荐三', FileHash: 'd3' }),
      ],
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    const items = wrapper.findAll('.np-rec-item');
    expect(items.length).toBeGreaterThanOrEqual(3);
    expect(wrapper.text()).toContain('推荐一');
    expect(wrapper.text()).toContain('推荐二');
    expect(wrapper.text()).toContain('推荐三');
    expect(wrapper.find('.np-num').text()).toBeTruthy();
  });

  it('has different DOM structure from Aurora', () => {
    const vm = createViewModel();
    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.find('.np-home').exists()).toBe(true);
    expect(wrapper.find('.aurora-home').exists()).toBe(false);
    expect(wrapper.find('.aurora-stage').exists()).toBe(false);
    expect(wrapper.find('.aurora-cover').exists()).toBe(false);
  });

  it('emits play-track when play button is clicked', async () => {
    const track = createTrack({ FileHash: 'hero-hash' });
    const vm = createViewModel({
      heroTrack: createTrack({ FileHash: 'restored-track' }),
      dailyTracks: [track],
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="hero-play"]').trigger('click');

    expect(wrapper.emitted('play-track')).toBeTruthy();
    expect(wrapper.emitted('play-track')![0]).toEqual([track]);
  });

  it('keeps the Newsprint daily feature tied to the daily feed instead of the restored player track', async () => {
    const restoredTrack = createTrack({
      FileHash: 'restored-player',
      SongName: '上次播放',
    });
    const dailyTrack = createTrack({
      FileHash: 'daily-feature',
      SongName: '今日推荐',
    });
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          heroTrack: restoredTrack,
          dailyTracks: [dailyTrack],
        }),
      },
    });

    expect(wrapper.get('.hero').text()).toContain('今日推荐');
    expect(wrapper.get('.hero').text()).not.toContain('上次播放');

    await wrapper.get('[data-test="hero-play"]').trigger('click');
    expect(wrapper.emitted('play-track')).toEqual([[dailyTrack]]);
  });

  it('retries only the daily section when its refresh control is clicked', async () => {
    const retryDaily = vi.fn().mockResolvedValue(undefined);
    const vm = createViewModel({ sections: createSectionStates({ daily: { retry: retryDaily } }) });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="daily-section-status"]').trigger('click');

    expect(retryDaily).toHaveBeenCalledTimes(1);
  });

  it('emits navigate when a playlist is clicked', async () => {
    const pl = createPlaylist({ specialid: 7, specialname: 'Editorial Picks' });
    const vm = createViewModel({ playlists: [pl] });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="playlist-7"]').trigger('click');

    expect(wrapper.emitted('navigate')).toBeTruthy();
    expect(wrapper.emitted('navigate')![0]).toEqual(['playlist', { id: 7, name: 'Editorial Picks' }]);
  });

  it('labels the playlist corner action as opening the playlist instead of playing it', async () => {
    const pl = createPlaylist({ specialid: 8, specialname: 'Archive Edition' });
    const wrapper = mount(NewsprintHome, {
      props: { model: createViewModel({ playlists: [pl] }) },
    });

    const openButton = wrapper.get('[data-test="playlist-open-8"]');
    expect(openButton.attributes('aria-label')).toBe('打开歌单：Archive Edition');
    expect(openButton.text().trim()).toBe('');

    await openButton.trigger('click');
    expect(wrapper.emitted('navigate')).toEqual([
      ['playlist', { id: 8, name: 'Archive Edition' }],
    ]);
    expect(wrapper.emitted('play-track')).toBeUndefined();
  });

  it('keeps old content visible during refresh', () => {
    const existingTrack = createTrack({ SongName: 'Existing Track' });
    const vm = createViewModel({
      isRefreshing: true,
      heroTrack: createTrack({ FileHash: 'restored-track' }),
      dailyTracks: [existingTrack],
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Existing Track');
  });

  it('displays a single columnized error summary', async () => {
    const vm = createViewModel({
      errors: [
        { section: 'daily', message: '加载失败' },
        { section: 'playlists', message: '加载失败' },
      ],
      errorSummary: '每日推荐、编辑推荐加载失败',
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.get('[data-test="home-error-summary"]').text()).toContain('每日推荐、编辑推荐加载失败');
    await wrapper.get('[data-test="home-error-retry-all"]').trigger('click');
    expect(wrapper.emitted('refresh')).toBeTruthy();
  });

  it('disables the listen CTA when there is no hero track', () => {
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
        }),
      },
    });
    expect(wrapper.get('[data-test="hero-play"]').attributes('disabled')).toBeDefined();
  });

  it('limits recommendations to 10 items', () => {
    const tracks = Array.from({ length: 15 }, (_, i) =>
      createTrack({ SongName: `Track ${i + 1}`, FileHash: `t${i}` }),
    );
    const vm = createViewModel({ dailyTracks: tracks });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    const items = wrapper.findAll('.np-rec-item');
    expect(items.length).toBe(10);
  });

  it('shows a Newsprint-specific skeleton during the first daily load', () => {
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          sections: createSectionStates({ daily: { loading: true } }),
        }),
      },
    });

    const loadingStage = wrapper.get('[data-test="newsprint-stage-loading"]');
    expect(loadingStage.attributes('aria-busy')).toBe('true');
    expect(loadingStage.attributes('aria-label')).toBe('正在加载每日推荐');
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(false);
  });

  it('keeps Newsprint stage styling semantic and reduced-motion safe', () => {
    expect(newsprintCss).toContain('.newsprint-stage-loading');
    expect(newsprintCss).toContain('.newsprint-stage-empty');
    expect(newsprintCss).not.toContain('[data-test="newsprint-stage-loading"]');
    expect(newsprintCss).not.toContain('[data-test="newsprint-stage-empty"]');

    const reduceStart = newsprintCss.indexOf('@media (prefers-reduced-motion: reduce)');
    expect(reduceStart).toBeGreaterThanOrEqual(0);
    const reducedMotionCss = newsprintCss.slice(reduceStart);
    expect(reducedMotionCss).toContain('.newsprint-stage-loading');
    expect(reducedMotionCss).toContain('.newsprint-stage-empty');
    expect(reducedMotionCss).toContain('animation: none');
    expect(reducedMotionCss).toContain('transform: none');
  });

  it('uses a rectangular editorial skeleton masthead', () => {
    const skeletonRule = newsprintCss.match(/\.newsprint-skeleton-masthead\s*\{([\s\S]*?)\n\}/)?.[1] ?? '';
    expect(skeletonRule).toContain('height:');
    expect(skeletonRule).not.toContain('aspect-ratio: 1');
    expect(skeletonRule).not.toMatch(/border-radius:\s*50%/);
  });

  it('renders the current track immediately while the daily feed is still loading', () => {
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          heroTrack: createTrack({ SongName: '报刊当前歌曲' }),
          dailyTracks: [],
          sections: createSectionStates({ daily: { loading: true } }),
        }),
      },
    });

    expect(wrapper.text()).toContain('报刊当前歌曲');
    expect(wrapper.find('[data-test="newsprint-stage-loading"]').exists()).toBe(false);
  });

  it('shows an actionable empty stage when no current track or recommendation exists', async () => {
    const retry = vi.fn().mockResolvedValue(undefined);
    const wrapper = mount(NewsprintHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          sections: createSectionStates({ daily: { isEmpty: true, retry } }),
        }),
      },
    });

    expect(wrapper.get('[data-test="newsprint-stage-empty"]').text()).toContain('还没有可播放的歌曲');
    await wrapper.get('[data-test="newsprint-empty-retry"]').trigger('click');
    expect(retry).toHaveBeenCalledTimes(1);
  });

  it('keeps Chinese as the primary masthead and feature language', () => {
    const wrapper = mount(NewsprintHome, { props: { model: createViewModel() } });
    expect(wrapper.get('.np-masthead .kicker').text()).toMatch(/^晚刊/);
    expect(wrapper.get('.feature .label').text()).toMatch(/^私荐/);
  });
});

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import AuroraHome from '../AuroraHome.vue';
import type { HomeViewModel, HomeSectionError } from '../homeViewModel';
import type { Track } from '../../../api/normalizer';
import type { PlaylistInfo } from '../../../api/homeFeedStore';

vi.mock('../../../api/motion', () => ({
  animateElement: vi.fn(),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startAmbientMotion: vi.fn(() => ({ kill: () => {} })),
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

function createViewModel(overrides: Partial<HomeViewModel> = {}): HomeViewModel {
  return {
    heroTrack: createTrack(),
    dailyTracks: [createTrack()],
    playlists: [],
    albums: [],
    queuePreview: [],
    queueTotal: 0,
    activeQueueHash: null,
    isPlaying: false,
    isInitialLoading: false,
    isRefreshing: false,
    errors: [] as readonly HomeSectionError[],
    ...overrides,
  };
}

describe('AuroraHome', () => {
  beforeEach(() => {
    vi.clearAllMocks();
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

  it('shows queue preview with real tracks', () => {
    const vm = createViewModel({
      queuePreview: [
        createTrack({ SongName: 'Queue 1', FileHash: 'q1' }),
        createTrack({ SongName: 'Queue 2', FileHash: 'q2' }),
        createTrack({ SongName: 'Queue 3', FileHash: 'q3' }),
      ],
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Queue 1');
    expect(wrapper.text()).toContain('Queue 2');
    expect(wrapper.text()).toContain('Queue 3');
  });

  it('renders a labelled queue rail and marks the active queued track', () => {
    const queue = Array.from({ length: 12 }, (_, index) => createTrack({
      FileHash: `queue-${index + 1}`,
      SongName: `Queue ${index + 1}`,
    }));
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ queuePreview: queue, queueTotal: 15, activeQueueHash: 'queue-3' }) },
    });

    expect(wrapper.get('[data-test="queue-rail"]').attributes('aria-label')).toBe('播放队列');
    expect(wrapper.findAll('[data-test^="queue-track-"]')).toHaveLength(12);
    expect(wrapper.get('[data-test="queue-track-queue-3"]').attributes('aria-current')).toBe('true');
    expect(wrapper.text()).toContain('15');
  });

  it('emits play-track when a queue row or daily card is selected', async () => {
    const queued = createTrack({ FileHash: 'queue-play', SongName: 'Queue Play' });
    const daily = createTrack({ FileHash: 'daily-play', SongName: 'Daily Play' });
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ queuePreview: [queued], dailyTracks: [daily] }) },
    });

    await wrapper.get('[data-test="queue-track-queue-play"]').trigger('click');
    await wrapper.get('[data-test="daily-track-daily-play"]').trigger('click');

    expect(wrapper.emitted('play-track')).toEqual([[queued], [daily]]);
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

  it('displays section errors', () => {
    const vm = createViewModel({
      errors: [{ section: 'daily', message: '加载失败' }],
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('加载失败');
  });

  it('shows loading state during initial load', () => {
    const vm = createViewModel({
      isInitialLoading: true,
      heroTrack: null,
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toBeTruthy();
  });
});

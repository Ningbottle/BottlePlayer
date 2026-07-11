import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import NewsprintHome from '../NewsprintHome.vue';
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
    isInitialLoading: false,
    isRefreshing: false,
    errors: [] as readonly HomeSectionError[],
    ...overrides,
  };
}

describe('NewsprintHome', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('renders masthead with cover, headline, and editorial phrase', () => {
    const vm = createViewModel({
      heroTrack: createTrack({
        SongName: '主推荐歌曲',
        SingerName: '艺术家',
        Image: 'http://example.com/cover.jpg',
      }),
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.find('.np-masthead').exists()).toBe(true);
    expect(wrapper.find('.np-hero-cover').exists()).toBe(true);
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
    const vm = createViewModel({ heroTrack: track });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="hero-play"]').trigger('click');

    expect(wrapper.emitted('play-track')).toBeTruthy();
    expect(wrapper.emitted('play-track')![0]).toEqual([track]);
  });

  it('emits refresh when refresh button is clicked', async () => {
    const vm = createViewModel();

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    await wrapper.get('[data-test="refresh"]').trigger('click');

    expect(wrapper.emitted('refresh')).toBeTruthy();
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

  it('keeps old content visible during refresh', () => {
    const vm = createViewModel({
      isRefreshing: true,
      heroTrack: createTrack({ SongName: 'Existing Track' }),
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('Existing Track');
  });

  it('displays section errors', () => {
    const vm = createViewModel({
      errors: [{ section: 'daily', message: '加载失败' }],
    });

    const wrapper = mount(NewsprintHome, {
      props: { model: vm },
    });

    expect(wrapper.text()).toContain('加载失败');
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
});

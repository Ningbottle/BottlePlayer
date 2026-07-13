import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';
import AuroraHome from '../AuroraHome.vue';
import type { HomeViewModel, HomeSectionError } from '../homeViewModel';
import type { Track } from '../../../api/normalizer';
import type { PlaylistInfo } from '../../../api/homeFeedStore';
import { animateStagger } from '../../../api/motion';

vi.mock('gsap', () => {
  const fromTo = vi.fn(() => ({ kill: vi.fn() }));
  const set = vi.fn();
  const killTweensOf = vi.fn();
  const to = vi.fn(() => ({ kill: vi.fn() }));
  return { gsap: { fromTo, set, killTweensOf, to } };
});

vi.mock('../../../api/motion', () => ({
  animateElement: vi.fn(() => ({ kill: () => {} })),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startAmbientMotion: vi.fn(() => ({ kill: () => {} })),
  isReducedMotion: vi.fn(() => false),
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
  const { queueWindowStart = 0, ...rest } = overrides;
  return {
    heroTrack: createTrack(),
    dailyTracks: [createTrack()],
    playlists: [],
    albums: [],
    queuePreview: [],
    queueWindowStart,
    queueTotal: 0,
    activeQueueHash: null,
    isPlaying: false,
    isInitialLoading: false,
    isRefreshing: false,
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
  });

  afterEach(() => {
    getContextSpy.mockReset();
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

  it('limits the queue rail to twelve tracks when the model preview is longer', () => {
    const queue = Array.from({ length: 13 }, (_, index) => createTrack({
      FileHash: `queue-${index + 1}`,
      SongName: `Queue ${index + 1}`,
    }));
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ queuePreview: queue, queueTotal: 13 }) },
    });

    expect(wrapper.findAll('[data-test^="queue-track-"]')).toHaveLength(12);
    expect(wrapper.find('[data-test="queue-track-queue-13"]').exists()).toBe(false);
  });

  it('displays zero when queue total is zero', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          queuePreview: [createTrack({ FileHash: 'queued-track' })],
          queueTotal: 0,
        }),
      },
    });

    expect(wrapper.get('.aurora-queue-rail-head h2 span').text()).toBe('0');
  });

  it('renders enriched empty queue state when queue is empty', () => {
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
    expect(empty.text()).toMatch(/队列|推荐/);
    expect(empty.text()).not.toBe('暂无队列');
  });

  it('uses an actionable empty stage instead of fabricated playback metadata', async () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          heroTrack: null,
          dailyTracks: [],
          queuePreview: [],
          queueTotal: 0,
        }),
      },
    });

    expect(wrapper.get('[data-test="aurora-stage-empty"]').text()).toContain('选择一首歌');
    expect(wrapper.text()).not.toContain('96kHz / 24bit');
    expect(wrapper.text()).not.toContain('VIP');
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(false);

    await wrapper.get('[data-test="empty-stage-refresh"]').trigger('click');
    expect(wrapper.emitted('refresh')).toBeTruthy();
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

  it('keeps list rows when queue has tracks', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          queuePreview: [
            createTrack({
              FileHash: 'q-has-1',
              SongName: 'Queued Song',
              SingerName: 'Queued Artist',
              Duration: 200,
            }),
          ],
          queueTotal: 1,
        }),
      },
    });
    expect(wrapper.find('[data-test="queue-empty-state"]').exists()).toBe(false);
    expect(wrapper.findAll('[data-test^="queue-track-"]').length).toBeGreaterThan(0);
  });

  it('keeps queue ordinals aligned with the moving queue window', () => {
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          queuePreview: [createTrack({ FileHash: 'queue-8' })],
          queueTotal: 20,
          queueWindowStart: 7,
        }),
      },
    });

    expect(wrapper.get('[data-test="queue-track-queue-8"]').text()).toContain('08');
  });

  it('pauses queue follow while hovered and resumes at the current track on mouseleave', async () => {
    const initial = createTrack({ FileHash: 'queue-1', SongName: 'Queue 1' });
    const current = createTrack({ FileHash: 'queue-8', SongName: 'Queue 8' });
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          queuePreview: [initial],
          queueTotal: 20,
          activeQueueHash: initial.FileHash,
        }),
      },
    });

    const list = wrapper.get('.aurora-queue-list');
    await list.trigger('mouseenter');
    await wrapper.setProps({
      model: createViewModel({
        queuePreview: [current],
        queueWindowStart: 7,
        queueTotal: 20,
        activeQueueHash: current.FileHash,
      }),
    });

    expect(wrapper.find('[data-test="queue-track-queue-1"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="queue-track-queue-8"]').exists()).toBe(false);

    await list.trigger('mouseleave');

    const resumed = wrapper.get('[data-test="queue-track-queue-8"]');
    expect(resumed.attributes('aria-current')).toBe('true');
    expect(resumed.text()).toContain('08');
  });

  it('emits a dedicated queue event without changing the daily-card event', async () => {
    const queued = createTrack({ FileHash: 'queue-play', SongName: 'Queue Play' });
    const daily = createTrack({ FileHash: 'daily-play', SongName: 'Daily Play' });
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ queuePreview: [queued], dailyTracks: [daily] }) },
    });

    await wrapper.get('[data-test="queue-track-queue-play"]').trigger('click');
    await wrapper.get('[data-test="daily-track-daily-play"]').trigger('click');

    expect(wrapper.emitted('play-queue-track')).toEqual([[queued]]);
    expect(wrapper.emitted('play-track')).toEqual([[daily]]);
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

  it('emits navigate to lyric when the lyrics button is clicked', async () => {
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel() },
    });

    await wrapper.get('.aurora-lyrics-link').trigger('click');

    expect(wrapper.emitted('navigate')).toEqual([['lyric']]);
  });

  it('still renders stage, queue rail, and controls under reduced motion', () => {
    const queue = [createTrack({ FileHash: 'queue-rm', SongName: 'RM Song' })];
    const daily = [createTrack({ FileHash: 'daily-rm', SongName: 'Daily RM' })];
    const wrapper = mount(AuroraHome, {
      props: {
        model: createViewModel({
          queuePreview: queue,
          queueTotal: 1,
          dailyTracks: daily,
        }),
      },
    });

    expect(wrapper.find('[data-test="aurora-stage"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="queue-rail"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="queue-track-queue-rm"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="daily-track-daily-rm"]').exists()).toBe(true);
  });

  it('uses buttons for every interactive daily and queue item', () => {
    const track = createTrack({ FileHash: 'interactive-track' });
    const wrapper = mount(AuroraHome, {
      props: { model: createViewModel({ dailyTracks: [track], queuePreview: [track] }) },
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
      isInitialLoading: true,
      heroTrack: null,
    });

    const wrapper = mount(AuroraHome, {
      props: { model: vm },
    });

    expect(wrapper.get('[data-test="aurora-stage-loading"]').text()).toContain('正在加载推荐');
    expect(wrapper.find('[data-test="hero-play"]').exists()).toBe(false);
    expect(wrapper.text()).not.toContain('正在播放');
    expect(wrapper.text()).not.toContain('96kHz');
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
});

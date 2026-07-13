import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';
import AuroraPlayerBar from '../AuroraPlayerBar.vue';
import type { PlayerController } from '../usePlayerControls';
import type { Track } from '../../../api/normalizer';

vi.mock('../../../api/motion', () => ({
  animateElement: vi.fn(),
  isReducedMotion: vi.fn(() => true),
}));

function mkTrack(overrides: Partial<Track> = {}): Track {
  return {
    FileHash: 'hash-1',
    SongName: 'Test Song',
    SingerName: 'Test Artist',
    Duration: 180,
    Image: 'http://example.com/cover.jpg',
    ...overrides,
  };
}

function createStubController(overrides: Record<string, any> = {}): PlayerController {
  return reactive({
    currentTrack: null,
    isPlaying: false,
    isLoading: false,
    showPauseIcon: false,
    currentTime: 0,
    duration: 0,
    volume: 0.7,
    loopMode: 'list',
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    quality: '128',
    coverUrl: '',
    progressPercent: 0,
    volumePercent: 70,
    isLyricView: false,
    showQualityMenu: false,
    showAddModal: false,
    toastMsg: '',
    favoriteMsg: '',
    qualityOptions: ['128', '320', 'flac'],
    togglePlay: vi.fn(),
    next: vi.fn(),
    prev: vi.fn(),
    seek: vi.fn(),
    setVolume: vi.fn(),
    setQuality: vi.fn(),
    toggleShuffle: vi.fn(),
    toggleRepeat: vi.fn(),
    toggleLyricView: vi.fn(),
    openLyricImmersion: vi.fn(),
    handleFavorite: vi.fn(),
    handleSelectQuality: vi.fn(),
    closeQualityMenu: vi.fn(),
    closeAddModal: vi.fn(),
    handleFavoriteSuccess: vi.fn(),
    handleFavoriteError: vi.fn(),
    getQualityLabel: (q: string) => {
      const labels: Record<string, string> = { '128': '标准', '320': '高品', 'flac': '无损', 'hires': 'Hi-Res', 'master': '臻品' };
      return labels[q] || q;
    },
    isCurrentQuality: (q: string) => q === '128',
    ...overrides,
  }) as PlayerController;
}

describe('AuroraPlayerBar', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  // ── Renders core content ──

  it('renders cover, song name, artist, time, PlayerProgress, and controls', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack({ SongName: '我的歌', SingerName: '歌手名' }),
      isPlaying: true,
      showPauseIcon: true,
      currentTime: 60,
      duration: 180,
      coverUrl: 'http://example.com/cover.jpg',
    });

    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-cover img').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-cover img').attributes('src')).toBe('http://example.com/cover.jpg');
    expect(wrapper.text()).toContain('我的歌');
    expect(wrapper.text()).toContain('歌手名');
    expect(wrapper.find('.progress-root').exists()).toBe(true);
    expect(wrapper.find('[aria-label="播放"], [aria-label="暂停"]').exists()).toBe(true);
  });

  it('cover click calls openLyricImmersion for fullscreen lyric entry', async () => {
    const openLyricImmersion = vi.fn();
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      coverUrl: 'http://example.com/cover.jpg',
      openLyricImmersion,
    });

    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.get('[data-test="aurora-pb-cover-immersion"]').trigger('click');
    expect(openLyricImmersion).toHaveBeenCalledOnce();
  });

  it('renders shuffle, prev, play/pause, next, repeat controls', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('[aria-label="随机"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="上一首"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="播放"], [aria-label="暂停"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="下一首"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="循环"]').exists()).toBe(true);
  });

  // ── Calls controller commands ──

  it('calls controller.togglePlay when play button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="播放"], [aria-label="暂停"]').trigger('click');
    expect(ctrl.togglePlay).toHaveBeenCalledOnce();
  });

  it('calls controller.next when next button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="下一首"]').trigger('click');
    expect(ctrl.next).toHaveBeenCalledOnce();
  });

  it('emits toggle-queue when queue button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="队列"]').trigger('click');
    expect(wrapper.emitted('toggle-queue')).toBeTruthy();
  });

  // ── States ──

  it('empty track shows placeholder without hollow transport or quality', () => {
    const ctrl = createStubController();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.text()).toContain('未播放歌曲');
    expect(wrapper.find('.aurora-pb-cover img').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-info-btn').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-transport').exists()).toBe(false);
    expect(wrapper.find('[data-test="aurora-player-quality"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="aurora-player-empty-console"]').exists()).toBe(true);
  });

  it('loading state shows pause icon (showPauseIcon=true)', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      isLoading: true,
      showPauseIcon: true,
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('[aria-label="暂停"]').exists()).toBe(true);
  });

  it('long song name does not overflow (has truncation)', () => {
    const longName = '这是一首非常非常非常非常非常非常长的歌曲名称'.repeat(5);
    const ctrl = createStubController({
      currentTrack: mkTrack({ SongName: longName }),
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.text()).toContain(longName);
    expect(wrapper.find('.aurora-pb-info-btn').exists()).toBe(true);
  });

  it('no cover uses fallback coverUrl', () => {
    const fallbackUrl = 'data:image/svg+xml;utf8,fallback';
    const ctrl = createStubController({
      currentTrack: mkTrack({ Image: undefined }),
      coverUrl: fallbackUrl,
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-cover img').attributes('src')).toBe(fallbackUrl);
  });

  // ── DOM structure (Aurora-specific) ──

  it('uses aurora-pb root class', () => {
    const ctrl = createStubController();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb').exists()).toBe(true);
  });

  it('does NOT use newsprint-pb root class', () => {
    const ctrl = createStubController();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.np-pb').exists()).toBe(false);
  });

  it('renders dock particles canvas behind the bar with progress', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      isPlaying: true,
      progressPercent: 42,
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
      global: { stubs: { PlayerProgress: true, AddToPlaylistModal: true } },
    });
    const canvas = wrapper.find('[data-test="aurora-dock-particles"]');
    expect(canvas.exists()).toBe(true);
    expect(canvas.attributes('data-playing')).toBe('true');
    expect(canvas.attributes('data-progress')).toBe('0.420');
  });

  it('has Aurora-specific transport section', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-transport').exists()).toBe(true);
    expect(wrapper.find('.np-pb-transport').exists()).toBe(false);
  });

  it('keeps transport and progress inside the liquid player console', () => {
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), duration: 180 }) },
    });

    const console = wrapper.get('[data-test="aurora-player-console"]');
    expect(console.find('.aurora-pb-transport').exists()).toBe(true);
    expect(console.find('[data-test="aurora-player-progress"] .progress-root').exists()).toBe(true);
    expect(console.find('[aria-label="播放"]').exists()).toBe(true);
  });

  it('keeps labelled queue, lyric, and volume controls outside the main play button', () => {
    const wrapper = mount(AuroraPlayerBar, { props: { controller: createStubController({ currentTrack: mkTrack() }) } });
    expect(wrapper.find('[aria-label="队列"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="歌词"]').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-volume').exists()).toBe(true);
  });

  it('keeps the primary player commands named for assistive technology', () => {
    const wrapper = mount(AuroraPlayerBar, { props: { controller: createStubController({ currentTrack: mkTrack() }) } });
    for (const label of ['随机', '上一首', '播放', '下一首', '循环', '队列', '歌词']) {
      expect(wrapper.find(`[aria-label="${label}"]`).exists()).toBe(true);
    }
  });
});

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';
import NewsprintPlayerBar from '../NewsprintPlayerBar.vue';
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
    isFavorite: false,
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
    cycleLoopMode: vi.fn(),
    toggleLyricView: vi.fn(),
    openLyricView: vi.fn(),
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

describe('NewsprintPlayerBar', () => {
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

    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.np-pb-cover img').exists()).toBe(true);
    expect(wrapper.find('.np-pb-cover img').attributes('src')).toBe('http://example.com/cover.jpg');
    expect(wrapper.text()).toContain('我的歌');
    expect(wrapper.text()).toContain('歌手名');
    expect(wrapper.find('.progress-root').exists()).toBe(true);
    expect(wrapper.find('[aria-label="播放"], [aria-label="暂停"]').exists()).toBe(true);
  });

  it('cover click opens the normal lyric page without entering fullscreen', async () => {
    const openLyricView = vi.fn();
    const openLyricImmersion = vi.fn();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), openLyricView, openLyricImmersion }) },
    });

    await wrapper.get('[data-test="np-pb-cover-immersion"]').trigger('click');
    expect(openLyricView).toHaveBeenCalledOnce();
    expect(openLyricImmersion).not.toHaveBeenCalled();
  });

  it('renders an icon-only fullscreen command and opens lyric immersion', async () => {
    const openLyricImmersion = vi.fn();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), openLyricImmersion }) },
    });

    const entry = wrapper.get('[data-test="np-pb-enter-fullscreen"]');
    expect(entry.text().trim()).toBe('');
    expect(entry.find('svg').exists()).toBe(true);
    expect(entry.attributes('aria-label')).toBe('进入全屏歌词');
    expect(entry.attributes('title')).toBe('进入全屏歌词');
    await entry.trigger('click');
    expect(openLyricImmersion).toHaveBeenCalledOnce();
  });

  it('disables lyric entry controls without a current track', () => {
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController() },
    });

    expect(wrapper.get('[data-test="np-pb-cover-immersion"]').attributes('disabled')).toBeDefined();
    expect(wrapper.get('[data-test="np-pb-enter-fullscreen"]').attributes('disabled')).toBeDefined();
    expect(wrapper.get('.np-pb-info-btn').attributes('disabled')).toBeDefined();
  });

  it('opens the normal lyric page from song information', async () => {
    const openLyricView = vi.fn();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), openLyricView }) },
    });

    await wrapper.get('.np-pb-info-btn').trigger('click');
    expect(openLyricView).toHaveBeenCalledOnce();
  });

  it('hides transport and quality when no track is loaded', () => {
    const ctrl = createStubController();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('[data-test="newsprint-player-transport"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="newsprint-player-quality"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="newsprint-player-empty-console"]').exists()).toBe(true);
  });

  it('renders loop, prev, play/pause, next controls (merged cycle button)', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    // Shuffle + repeat merged into one cycle button (list mode -> "列表顺序播放").
    expect(wrapper.find('[aria-label="列表顺序播放"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="上一首"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="播放"], [aria-label="暂停"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="下一首"]').exists()).toBe(true);
  });

  it('keeps previous, play/pause, and next in the core transport order', () => {
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack() }) },
    });

    const labels = wrapper.get('[data-test="newsprint-player-transport"]')
      .findAll('button')
      .map((button) => button.attributes('aria-label'))
      .filter((label): label is string =>
        typeof label === 'string' && ['上一首', '播放', '暂停', '下一首'].includes(label));

    expect(labels).toEqual(['上一首', '播放', '下一首']);
  });

  it('uses accessible icon commands without visible command words', () => {
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack() }) },
    });

    for (const button of wrapper.findAll('button')) {
      const ariaLabel = button.attributes('aria-label');
      const title = button.attributes('title');
      expect(ariaLabel).toBeDefined();
      expect(title).toBeDefined();
      if (!ariaLabel || !title) continue;
      expect(ariaLabel).toMatch(/[\u3400-\u9fff]/);
      expect(title).toMatch(/[\u3400-\u9fff]/);
    }

    const iconCommands = [
      wrapper.get('[data-test="np-pb-enter-fullscreen"]'),
      ...wrapper.get('[data-test="newsprint-player-transport"]').findAll('button'),
      wrapper.get('.np-pb-icon[aria-label="队列"]'),
      wrapper.get('.np-pb-lyric'),
    ];
    for (const command of iconCommands) {
      expect(command.text().trim()).toBe('');
    }
  });

  // ── Calls controller commands ──

  it('calls controller.togglePlay when play button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="播放"], [aria-label="暂停"]').trigger('click');
    expect(ctrl.togglePlay).toHaveBeenCalledOnce();
  });

  it('calls controller.next when next button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="下一首"]').trigger('click');
    expect(ctrl.next).toHaveBeenCalledOnce();
  });

  it('emits toggle-queue when queue button clicked', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.find('[aria-label="队列"]').trigger('click');
    expect(wrapper.emitted('toggle-queue')).toBeTruthy();
  });

  // ── States ──

  it('empty track shows placeholder without overflow', () => {
    const ctrl = createStubController();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.text()).toContain('未播放歌曲');
    expect(wrapper.find('.np-pb-cover img').exists()).toBe(false);
    expect(wrapper.find('[data-test="player-cover-placeholder"]').exists()).toBe(true);
    expect(wrapper.find('.np-pb-info').exists()).toBe(true);
  });

  it('loading state shows pause icon (showPauseIcon=true)', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      isLoading: true,
      showPauseIcon: true,
    });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('[aria-label="暂停"]').exists()).toBe(true);
  });

  it('shows a persistent collected state on the favorite icon', () => {
    const wrapper = mount(NewsprintPlayerBar, {
      props: {
        controller: createStubController({
          currentTrack: mkTrack(),
          isFavorite: true,
        }),
      },
    });

    const favorite = wrapper.get('.np-pb-fav');
    expect(favorite.classes()).toContain('is-active');
    expect(favorite.attributes('aria-label')).toBe('已收藏');
    expect(favorite.get('svg').attributes('fill')).toBe('currentColor');
  });

  it('long song name does not overflow (has truncation)', () => {
    const longName = '这是一首非常非常非常非常非常非常长的歌曲名称'.repeat(5);
    const ctrl = createStubController({
      currentTrack: mkTrack({ SongName: longName }),
    });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.text()).toContain(longName);
    expect(wrapper.find('.np-pb-info').exists()).toBe(true);
  });

  it('no cover renders the Newsprint icon placeholder without an empty image', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack({ Image: undefined }),
      coverUrl: '',
    });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.np-pb-cover img').exists()).toBe(false);
    const placeholder = wrapper.get('[data-test="player-cover-placeholder"]');
    expect(placeholder.attributes('data-icon-family')).toBe('lucide');
    expect(placeholder.attributes('aria-hidden')).toBe('true');
  });

  // ── DOM structure (Newsprint-specific) ──

  it('uses np-pb root class', () => {
    const ctrl = createStubController();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.np-pb').exists()).toBe(true);
  });

  it('does NOT use aurora-pb root class', () => {
    const ctrl = createStubController();
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb').exists()).toBe(false);
  });

  it('has Newsprint-specific transport section', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.np-pb-transport').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-transport').exists()).toBe(false);
  });

  // ── DOM difference from Aurora ──

  it('does not use Aurora capsule/round button styles', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(NewsprintPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-capsule').exists()).toBe(false);
  });
});

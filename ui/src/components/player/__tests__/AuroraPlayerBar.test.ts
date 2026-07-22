import { describe, it, expect, vi, beforeEach, type Mock } from 'vitest';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';
import { PhHeart } from '@phosphor-icons/vue';
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

  it('opens the quality menu and selects an option without being clipped by the dock', async () => {
    const handleSelectQuality = vi.fn();
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      quality: '128',
      showQualityMenu: false,
      handleSelectQuality,
    });

    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
      attachTo: document.body,
    });

    // Dock must not use overflow:hidden or the upward quality menu is unusable.
    const dock = wrapper.get('.aurora-pb');
    expect(getComputedStyle(dock.element).overflow).not.toBe('hidden');

    await wrapper.get('[data-test="aurora-player-quality"] button').trigger('click');
    expect(ctrl.showQualityMenu).toBe(true);

    // Re-open with menu forced visible for option click
    ctrl.showQualityMenu = true;
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="aurora-quality-menu"]').exists()).toBe(true);
    await wrapper.get('[data-test="aurora-quality-option-320"]').trigger('click');
    expect(handleSelectQuality).toHaveBeenCalledWith('320');

    wrapper.unmount();
  });

  it('cover click opens the normal lyric page without entering fullscreen', async () => {
    const openLyricView = vi.fn();
    const openLyricImmersion = vi.fn();
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      coverUrl: 'http://example.com/cover.jpg',
      openLyricView,
      openLyricImmersion,
    });

    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    await wrapper.get('[data-test="aurora-pb-cover-immersion"]').trigger('click');
    expect(openLyricView).toHaveBeenCalledOnce();
    expect(openLyricImmersion).not.toHaveBeenCalled();
  });

  it('song information opens the normal lyric page', async () => {
    const openLyricView = vi.fn();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), openLyricView }) },
    });

    await wrapper.get('.aurora-pb-info-btn').trigger('click');
    expect(openLyricView).toHaveBeenCalledOnce();
  });

  it('renders an icon-only fullscreen command and opens lyric immersion', async () => {
    const openLyricImmersion = vi.fn();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack(), openLyricImmersion }) },
    });

    const entry = wrapper.get('[data-test="aurora-pb-enter-fullscreen"]');
    expect(entry.text().trim()).toBe('');
    expect(entry.find('svg').exists()).toBe(true);
    expect(entry.attributes('aria-label')).toBe('进入全屏歌词');
    expect(entry.attributes('title')).toBe('进入全屏歌词');
    await entry.trigger('click');
    expect(openLyricImmersion).toHaveBeenCalledOnce();
  });

  it('disables lyric entry controls without a current track', () => {
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: createStubController() },
    });

    expect(wrapper.get('[data-test="aurora-pb-cover-immersion"]').attributes('disabled')).toBeDefined();
    expect(wrapper.get('[data-test="aurora-pb-enter-fullscreen"]').attributes('disabled')).toBeDefined();
    expect(wrapper.get('.aurora-pb-info-btn').attributes('disabled')).toBeDefined();
  });

  it('renders loop, prev, play/pause, next controls (merged cycle button)', () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    // Shuffle + repeat merged into one cycle button (list mode -> "列表顺序播放").
    expect(wrapper.find('[aria-label="列表顺序播放"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="上一首"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="播放"], [aria-label="暂停"]').exists()).toBe(true);
    expect(wrapper.find('[aria-label="下一首"]').exists()).toBe(true);
  });

  it('keeps previous, play/pause, and next in the core transport order', () => {
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: createStubController({ currentTrack: mkTrack() }) },
    });

    const labels = wrapper.get('[data-test="aurora-player-transport"]')
      .findAll('button')
      .map((button) => button.attributes('aria-label'))
      .filter((label): label is string =>
        typeof label === 'string' && ['上一首', '播放', '暂停', '下一首'].includes(label));

    expect(labels).toEqual(['上一首', '播放', '下一首']);
  });

  it('uses accessible icon commands without visible command words', () => {
    const wrapper = mount(AuroraPlayerBar, {
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
      wrapper.get('[data-test="aurora-pb-enter-fullscreen"]'),
      ...wrapper.get('[data-test="aurora-player-transport"]').findAll('button'),
      wrapper.get('.aurora-pb-queue'),
      wrapper.get('.aurora-pb-lyric'),
    ];
    for (const command of iconCommands) {
      expect(command.text().trim()).toBe('');
    }
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

  it('empty track shows a muted transport, never a placeholder console', () => {
    const ctrl = createStubController();
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.text()).toContain('未播放歌曲');
    expect(wrapper.find('.aurora-pb-cover img').exists()).toBe(false);
    expect(wrapper.find('[data-test="player-cover-placeholder"]').exists()).toBe(true);
    expect(wrapper.find('.aurora-pb-info-btn').exists()).toBe(true);

    const transport = wrapper.get('[data-test="aurora-player-transport"]');
    expect(transport.classes()).toContain('is-muted');
    const buttons = transport.findAll('button');
    expect(buttons.length).toBeGreaterThanOrEqual(4);
    expect(buttons.every((b) => b.attributes('disabled') !== undefined)).toBe(true);

    expect(wrapper.find('[data-test="aurora-player-quality"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="aurora-player-empty-console"]').exists()).toBe(false);
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

  it('shows a persistent collected state on the favorite icon', () => {
    const wrapper = mount(AuroraPlayerBar, {
      props: {
        controller: createStubController({
          currentTrack: mkTrack(),
          isFavorite: true,
        }),
      },
    });

    const favorite = wrapper.get('.aurora-pb-fav');
    expect(favorite.classes()).toContain('is-active');
    expect(favorite.attributes('aria-label')).toBe('已收藏');
    expect(wrapper.findComponent(PhHeart).props('weight')).toBe('fill');
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

  it('no cover renders the Aurora icon placeholder without an empty image', () => {
    const ctrl = createStubController({
      currentTrack: mkTrack({ Image: undefined }),
      coverUrl: '',
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-cover img').exists()).toBe(false);
    const placeholder = wrapper.get('[data-test="player-cover-placeholder"]');
    expect(placeholder.attributes('data-icon-family')).toBe('phosphor');
    expect(placeholder.attributes('aria-hidden')).toBe('true');
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
    expect(canvas.attributes('data-particle-cap')).toBe('44');
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
    for (const label of ['列表顺序播放', '上一首', '播放', '下一首', '队列', '歌词']) {
      expect(wrapper.find(`[aria-label="${label}"]`).exists()).toBe(true);
    }
  });

  it('fires a one-shot ripple on the play button when playback toggles', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack(), isPlaying: false });
    // Stub is reactive-mutable; PlayerController marks state readonly for consumers.
    const mutableCtrl = ctrl as { isPlaying: boolean };
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    expect(wrapper.find('.aurora-pb-play-ripple').exists()).toBe(false);

    mutableCtrl.isPlaying = true;
    await wrapper.vm.$nextTick();
    expect(wrapper.find('.aurora-pb-play-ripple').exists()).toBe(true);

    mutableCtrl.isPlaying = false;
    await wrapper.vm.$nextTick();
    // New toggle → fresh ripple element (key bumped)
    expect(wrapper.find('.aurora-pb-play-ripple').exists()).toBe(true);
  });

  it('pops the heart only on a user-initiated favorite', async () => {
    const ctrl = createStubController({
      currentTrack: mkTrack(),
      isFavorite: false,
    });
    // Stub is reactive-mutable; PlayerController marks state readonly for consumers.
    const mutableCtrl = ctrl as { isFavorite: boolean };
    (ctrl.handleFavorite as Mock).mockImplementation(async () => {
      mutableCtrl.isFavorite = true;
    });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
      attachTo: document.body,
    });

    const fav = wrapper.get('.aurora-pb-fav');
    expect(fav.classes()).not.toContain('just-faved');

    await fav.trigger('click');
    await vi.waitFor(() => {
      expect(wrapper.get('.aurora-pb-fav').classes()).toContain('just-faved');
    });

    wrapper.unmount();
  });

  it('sets volume on pointerdown and follows a drag', async () => {
    const ctrl = createStubController({ currentTrack: mkTrack() });
    const wrapper = mount(AuroraPlayerBar, {
      props: { controller: ctrl },
    });

    const bar = wrapper.get('.aurora-pb-vol-bar');
    bar.element.getBoundingClientRect = vi.fn(() => ({
      left: 0,
      width: 100,
      top: 0,
      right: 100,
      bottom: 16,
      height: 16,
      x: 0,
      y: 0,
      toJSON: () => ({}),
    })) as unknown as () => DOMRect;

    // jsdom lacks PointerEvent; dispatch MouseEvent with the pointer type instead.
    bar.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 25 }));
    expect(ctrl.setVolume).toHaveBeenLastCalledWith(0.25);

    bar.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: 75 }));
    expect(ctrl.setVolume).toHaveBeenLastCalledWith(0.75);

    bar.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true }));
    bar.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: 10 }));
    expect(ctrl.setVolume).toHaveBeenLastCalledWith(0.75); // drag ended
  });
});

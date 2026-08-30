import { describe, it, expect, vi, beforeEach } from 'vitest';
import type { Track } from '../../../api/normalizer';
import { __resetFavoriteMarkersForTests } from '../../../api/favoriteMarkers';

// ── Mock playerStore module ──
const mocks = vi.hoisted(() => ({
  togglePlay: vi.fn(),
  next: vi.fn(),
  prev: vi.fn(),
  seek: vi.fn(),
  setVolume: vi.fn(),
  setQuality: vi.fn(),
  setLyricFullscreen: vi.fn(),
  store: null as any,
}));

vi.mock('../../../api/playerStore', async () => {
  const { reactive } = await import('vue');
  mocks.store = reactive({
    currentTrack: null,
    isPlaying: false,
    isLoading: false,
    currentTime: 0,
    duration: 0,
    volume: 0.7,
    loopMode: 'list' as string,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    quality: '128',
  });
  return {
    playerStore: mocks.store,
    togglePlay: mocks.togglePlay,
    next: mocks.next,
    prev: mocks.prev,
    seek: mocks.seek,
    setVolume: mocks.setVolume,
    setQuality: mocks.setQuality,
  };
});

vi.mock('../../../api/lyricFullscreen', () => ({
  lyricFullscreen: { value: false },
  setLyricFullscreen: (...args: unknown[]) => mocks.setLyricFullscreen(...args),
}));

import { usePlayerControls } from '../usePlayerControls';

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

describe('usePlayerControls', () => {
  beforeEach(() => {
    localStorage.clear();
    mocks.store.currentTrack = null;
    mocks.store.isPlaying = false;
    mocks.store.isLoading = false;
    mocks.store.currentTime = 0;
    mocks.store.duration = 0;
    mocks.store.volume = 0.7;
    mocks.store.loopMode = 'list';
    mocks.store.errorMsg = '';
    mocks.store.isPreview = false;
    mocks.store.vipRequired = false;
    mocks.store.quality = '128';

    mocks.togglePlay.mockClear();
    mocks.next.mockClear();
    mocks.prev.mockClear();
    mocks.seek.mockClear();
    mocks.setVolume.mockClear();
    mocks.setQuality.mockClear();
    mocks.setLyricFullscreen.mockClear();
  });

  // ── Command delegation ──

  it('togglePlay calls playerStore.togglePlay', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.togglePlay();
    expect(mocks.togglePlay).toHaveBeenCalledOnce();
  });

  it('next calls playerStore.next', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.next();
    expect(mocks.next).toHaveBeenCalledOnce();
  });

  it('prev calls playerStore.prev', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.prev();
    expect(mocks.prev).toHaveBeenCalledOnce();
  });

  it('setVolume calls playerStore.setVolume', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.setVolume(0.5);
    expect(mocks.setVolume).toHaveBeenCalledWith(0.5);
  });

  it('setQuality calls playerStore.setQuality', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.setQuality('320');
    expect(mocks.setQuality).toHaveBeenCalledWith('320');
  });

  // ── Seek clamping ──

  it('seek clamps to [0, duration] and calls playerStore.seek once', () => {
    mocks.store.duration = 100;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });

    ctrl.seek(50);
    expect(mocks.seek).toHaveBeenCalledWith(50);
    expect(mocks.seek).toHaveBeenCalledTimes(1);

    mocks.seek.mockClear();
    ctrl.seek(150);
    expect(mocks.seek).toHaveBeenCalledWith(100);
    expect(mocks.seek).toHaveBeenCalledTimes(1);

    mocks.seek.mockClear();
    ctrl.seek(-10);
    expect(mocks.seek).toHaveBeenCalledWith(0);
    expect(mocks.seek).toHaveBeenCalledTimes(1);
  });

  it('seek with duration=0 clamps to 0', () => {
    mocks.store.duration = 0;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.seek(50);
    expect(mocks.seek).toHaveBeenCalledWith(0);
  });

  // ── toggleShuffle / toggleRepeat ──

  it('toggleShuffle switches loopMode between random and list', () => {
    mocks.store.loopMode = 'list';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.toggleShuffle();
    expect(mocks.store.loopMode).toBe('random');

    ctrl.toggleShuffle();
    expect(mocks.store.loopMode).toBe('list');
  });

  it('toggleRepeat switches loopMode between single and list', () => {
    mocks.store.loopMode = 'list';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.toggleRepeat();
    expect(mocks.store.loopMode).toBe('single');

    ctrl.toggleRepeat();
    expect(mocks.store.loopMode).toBe('list');
  });

  it('cycleLoopMode cycles list -> single -> random -> list', () => {
    mocks.store.loopMode = 'list';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.cycleLoopMode();
    expect(mocks.store.loopMode).toBe('single');
    ctrl.cycleLoopMode();
    expect(mocks.store.loopMode).toBe('random');
    ctrl.cycleLoopMode();
    expect(mocks.store.loopMode).toBe('list');
  });

  // ── toggleLyricView ──

  it('toggleLyricView navigates to lyric when activeView is not lyric', () => {
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });
    ctrl.toggleLyricView();
    expect(onNavigate).toHaveBeenCalledWith('lyric');
  });

  it('toggleLyricView navigates to home when activeView is lyric', () => {
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'lyric', onNavigate });
    ctrl.toggleLyricView();
    expect(onNavigate).toHaveBeenCalledWith('home');
  });

  it('openLyricView opens the normal lyric page and exits fullscreen', () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });

    ctrl.openLyricView();

    expect(onNavigate).toHaveBeenCalledWith('lyric');
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(false);
  });

  it('openLyricView keeps the lyric page open while leaving fullscreen', () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'lyric', onNavigate });

    ctrl.openLyricView();

    expect(onNavigate).not.toHaveBeenCalled();
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(false);
  });

  it('openLyricView is a no-op without a current track', () => {
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });

    ctrl.openLyricView();

    expect(onNavigate).not.toHaveBeenCalled();
    expect(mocks.setLyricFullscreen).not.toHaveBeenCalled();
  });

  it('openLyricImmersion navigates to lyric and enters fullscreen when not on lyric', () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });
    ctrl.openLyricImmersion();
    expect(onNavigate).toHaveBeenCalledWith('lyric');
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(true);
  });

  it('openLyricImmersion enters fullscreen without re-navigating when already on lyric', () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'lyric', onNavigate });
    ctrl.openLyricImmersion();
    expect(onNavigate).not.toHaveBeenCalled();
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(true);
  });

  it('openLyricImmersion does not enter fullscreen when navigate reports failure', async () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn().mockResolvedValue(false);
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });
    await ctrl.openLyricImmersion();
    expect(onNavigate).toHaveBeenCalledWith('lyric');
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(false);
    expect(mocks.setLyricFullscreen).not.toHaveBeenCalledWith(true);
  });

  it('openLyricImmersion consumes a rejected navigate promise and clears fullscreen', async () => {
    mocks.store.currentTrack = mkTrack();
    const onNavigate = vi.fn().mockRejectedValue(new Error('navigation aborted'));
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });
    const pending = ctrl.openLyricImmersion();
    expect(pending).toBeInstanceOf(Promise);
    await expect(pending).resolves.toBeUndefined();
    expect(onNavigate).toHaveBeenCalledWith('lyric');
    expect(mocks.setLyricFullscreen).toHaveBeenCalledWith(false);
    expect(mocks.setLyricFullscreen).not.toHaveBeenCalledWith(true);
  });

  it('openLyricImmersion is a no-op without a current track', () => {
    mocks.store.currentTrack = null;
    const onNavigate = vi.fn();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate });
    ctrl.openLyricImmersion();
    expect(onNavigate).not.toHaveBeenCalled();
    expect(mocks.setLyricFullscreen).not.toHaveBeenCalled();
  });

  // ── handleFavorite ──

  it('handleFavorite toggles favorite on when the track is not a favorite', () => {
    __resetFavoriteMarkersForTests();
    mocks.store.currentTrack = mkTrack({ FileHash: 'fav-toggle' });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.isFavorite).toBe(false);
    ctrl.handleFavorite();
    expect(ctrl.isFavorite).toBe(true);
  });

  it('handleFavorite toggles favorite off when the track is already a favorite', () => {
    __resetFavoriteMarkersForTests();
    mocks.store.currentTrack = mkTrack({ FileHash: 'fav-toggle' });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.handleFavorite(); // on
    expect(ctrl.isFavorite).toBe(true);
    ctrl.handleFavorite(); // off
    expect(ctrl.isFavorite).toBe(false);
  });

  it('handleFavorite does nothing when no track', () => {
    __resetFavoriteMarkersForTests();
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.handleFavorite();
    expect(ctrl.isFavorite).toBe(false);
  });

  it('handleFavorite shows a local/pending message (not premature confirmed success) when not logged in', async () => {
    __resetFavoriteMarkersForTests();
    mocks.store.currentTrack = mkTrack({ FileHash: 'anon-heart' });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    await ctrl.handleFavorite();
    expect(ctrl.isFavorite).toBe(true); // local favorite
    // Must NOT claim confirmed server-side success.
    expect(ctrl.favoriteMsg).not.toContain('已收藏到「我喜欢的音乐」');
    expect(ctrl.favoriteMsg.length).toBeGreaterThan(0);
  });

  it('closeAddModal closes the modal', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.showAddModal = true;
    ctrl.closeAddModal();
    expect(ctrl.showAddModal).toBe(false);
  });

  it('both skin player bars read the same shared favorite state', () => {
    __resetFavoriteMarkersForTests();
    mocks.store.currentTrack = mkTrack({ FileHash: 'shared-1' });
    // Two controller instances model the Aurora and Newsprint player bars,
    // which must read the SAME favorite store.
    const auroraCtrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    const newsprintCtrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(auroraCtrl.isFavorite).toBe(false);
    expect(newsprintCtrl.isFavorite).toBe(false);

    auroraCtrl.handleFavorite(); // favorite via one skin
    expect(auroraCtrl.isFavorite).toBe(true);
    expect(newsprintCtrl.isFavorite).toBe(true); // visible on the other skin

    newsprintCtrl.handleFavorite(); // unfavorite via the other skin
    expect(auroraCtrl.isFavorite).toBe(false);
    expect(newsprintCtrl.isFavorite).toBe(false);
  });

  it('marks the current track as collected after a successful add and restores the marker', () => {
    __resetFavoriteMarkersForTests();
    mocks.store.currentTrack = mkTrack({ FileHash: 'favorite-hash' });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });

    expect(ctrl.isFavorite).toBe(false);
    ctrl.handleFavoriteSuccess('我喜欢');
    expect(ctrl.isFavorite).toBe(true);

    const restored = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(restored.isFavorite).toBe(true);
  });

  // ── handleSelectQuality ──

  it('handleSelectQuality calls setQuality when quality differs', () => {
    mocks.store.quality = '128';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.handleSelectQuality('320');
    expect(mocks.setQuality).toHaveBeenCalledWith('320');
  });

  it('handleSelectQuality does nothing when quality is the same', () => {
    mocks.store.quality = '128';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    ctrl.handleSelectQuality('128');
    expect(mocks.setQuality).not.toHaveBeenCalled();
  });

  // ── View model ──

  it('currentTrack reflects playerStore.currentTrack', () => {
    const track = mkTrack({ SongName: 'My Song' });
    mocks.store.currentTrack = track;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.currentTrack).toStrictEqual(track);
  });

  it('showPauseIcon is true when isPlaying or isLoading', () => {
    mocks.store.isPlaying = false;
    mocks.store.isLoading = false;
    const ctrl1 = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl1.showPauseIcon).toBe(false);

    mocks.store.isPlaying = true;
    const ctrl2 = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl2.showPauseIcon).toBe(true);

    mocks.store.isPlaying = false;
    mocks.store.isLoading = true;
    const ctrl3 = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl3.showPauseIcon).toBe(true);
  });

  it('coverUrl returns track Image when available', () => {
    mocks.store.currentTrack = mkTrack({ Image: 'http://cover.jpg' });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.coverUrl).toBe('http://cover.jpg');
  });

  it('coverUrl is empty when track has no Image', () => {
    mocks.store.currentTrack = mkTrack({ Image: undefined });
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.coverUrl).toBe('');
  });

  it('coverUrl is empty when no track is active', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.coverUrl).toBe('');
  });

  it('progressPercent computes currentTime/duration * 100', () => {
    mocks.store.currentTime = 30;
    mocks.store.duration = 120;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.progressPercent).toBeCloseTo(25);
  });

  it('progressPercent is 0 when duration is 0', () => {
    mocks.store.currentTime = 30;
    mocks.store.duration = 0;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.progressPercent).toBe(0);
  });

  it('volumePercent computes volume * 100', () => {
    mocks.store.volume = 0.5;
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.volumePercent).toBe(50);
  });

  // ── Quality helpers ──

  it('getQualityLabel returns label for known quality', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.getQualityLabel('128')).toBe('标准');
    expect(ctrl.getQualityLabel('320')).toBe('高品');
    expect(ctrl.getQualityLabel('flac')).toBe('无损');
  });

  it('getQualityLabel returns input for unknown quality', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.getQualityLabel('unknown')).toBe('unknown');
  });

  it('isCurrentQuality returns true for current quality', () => {
    mocks.store.quality = '320';
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    expect(ctrl.isCurrentQuality('320')).toBe(true);
    expect(ctrl.isCurrentQuality('128')).toBe(false);
  });

  // ── Does not expose skin DOM ──

  it('controller does not expose skin-specific DOM properties', () => {
    const ctrl = usePlayerControls({ activeView: () => 'home', onNavigate: () => {} });
    const keys = Object.keys(ctrl);
    expect(keys).not.toContain('skinId');
    expect(keys).not.toContain('theme');
    expect(keys).not.toContain('el');
    expect(keys).not.toContain('rootEl');
  });
});

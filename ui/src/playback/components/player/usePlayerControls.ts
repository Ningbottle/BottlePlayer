import { computed, ref, reactive, getCurrentScope, onScopeDispose } from 'vue';
import {
  playerStore,
  togglePlay as storeTogglePlay,
  next as storeNext,
  prev as storePrev,
  seek as storeSeek,
  setVolume as storeSetVolume,
  setQuality as storeSetQuality,
} from '../../playerStore';
import { setLyricFullscreen } from '../../../api/lyricFullscreen';
import {
  isFavoriteMarker,
  markFavorite,
  reloadFavoriteMarkers,
  favoriteStore,
} from '../../../features/library';
import type { Track } from '../../../shared/music/track';
import type { LoopMode } from '../../playerStore';

export interface PlayerController {
  readonly currentTrack: Track | null;
  readonly isPlaying: boolean;
  readonly isLoading: boolean;
  readonly showPauseIcon: boolean;
  readonly currentTime: number;
  readonly duration: number;
  readonly volume: number;
  readonly loopMode: LoopMode;
  readonly errorMsg: string;
  readonly isPreview: boolean;
  readonly vipRequired: boolean;
  readonly quality: string;
  readonly coverUrl: string;
  readonly progressPercent: number;
  readonly volumePercent: number;
  readonly isLyricView: boolean;
  readonly isFavorite: boolean;

  showQualityMenu: boolean;
  showAddModal: boolean;
  toastMsg: string;
  favoriteMsg: string;
  qualityOptions: string[];

  togglePlay: () => void;
  next: () => void;
  prev: () => void;
  seek: (seconds: number) => void;
  setVolume: (vol: number) => void;
  setQuality: (q: string) => void;
  toggleShuffle: () => void;
  toggleRepeat: () => void;
  /** Cycle list -> single -> random -> list (single-button loop mode). */
  cycleLoopMode: () => void;
  toggleLyricView: () => void;
  /** Open the regular lyric page and leave fullscreen mode. */
  openLyricView: () => void;
  /** Open lyric view + fullscreen immersion (explicit fullscreen entry). */
  openLyricImmersion: () => void | Promise<void>;
  handleFavorite: () => Promise<void>;
  handleSelectQuality: (q: string) => void;
  closeQualityMenu: () => void;
  closeAddModal: () => void;
  handleFavoriteSuccess: (playlistName: string) => void;
  handleFavoriteError: (msg: string) => void;

  getQualityLabel: (q: string) => string;
  isCurrentQuality: (q: string) => boolean;
}

export interface UsePlayerControlsOptions {
  activeView: () => string;
  onNavigate: (view: string) => void | boolean | Promise<void | boolean>;
}

const qualityLabels: Record<string, string> = {
  '128': '标准',
  '320': '高品',
  'flac': '无损',
  'hires': 'Hi-Res',
  'master': '臻品',
};

const qualityOptions = ['128', '320', 'flac'];

export function usePlayerControls(options: UsePlayerControlsOptions): PlayerController {
  // Ensure markers hydrate if storage changed while bar was unmounted.
  reloadFavoriteMarkers();

  const currentTrack = computed(() => playerStore.currentTrack);
  const isPlaying = computed(() => playerStore.isPlaying);
  const isLoading = computed(() => playerStore.isLoading);
  const showPauseIcon = computed(() => isPlaying.value || isLoading.value);
  const currentTime = computed(() => playerStore.currentTime);
  const duration = computed(() => playerStore.duration);
  const volume = computed(() => playerStore.volume);
  const loopMode = computed(() => playerStore.loopMode);
  const errorMsg = computed(() => playerStore.errorMsg);
  const isPreview = computed(() => playerStore.isPreview);
  const vipRequired = computed(() => playerStore.vipRequired);
  const quality = computed(() => playerStore.quality);

  const coverUrl = computed(() => currentTrack.value?.Image || '');

  const progressPercent = computed(() => {
    if (!duration.value || duration.value <= 0) return 0;
    return Math.max(0, Math.min(100, (currentTime.value / duration.value) * 100));
  });

  const volumePercent = computed(() => volume.value * 100);

  const isLyricView = computed(() => options.activeView() === 'lyric');
  const isFavorite = computed(() => isFavoriteMarker(currentTrack.value?.FileHash));

  const showQualityMenu = ref(false);
  const showAddModal = ref(false);
  const toastMsg = ref('');
  const favoriteMsg = ref('');

  let toastTimer: ReturnType<typeof setTimeout> | null = null;
  let favToastTimer: ReturnType<typeof setTimeout> | null = null;

  function clearToastTimers() {
    if (toastTimer) {
      clearTimeout(toastTimer);
      toastTimer = null;
    }
    if (favToastTimer) {
      clearTimeout(favToastTimer);
      favToastTimer = null;
    }
  }

  // Only register when called inside a component/effect scope (avoids test noise).
  if (getCurrentScope()) {
    onScopeDispose(() => {
      clearToastTimers();
    });
  }

  function showToast(msg: string) {
    toastMsg.value = msg;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toastMsg.value = ''; toastTimer = null; }, 2000);
  }

  function togglePlay() {
    storeTogglePlay();
  }

  function next() {
    storeNext();
  }

  function prev() {
    storePrev();
  }

  function seek(seconds: number) {
    const clamped = Math.max(0, Math.min(duration.value, seconds));
    storeSeek(clamped);
  }

  function setVolume(vol: number) {
    storeSetVolume(vol);
  }

  function setQuality(q: string) {
    storeSetQuality(q);
  }

  function toggleShuffle() {
    const isRandom = loopMode.value === 'random';
    playerStore.loopMode = isRandom ? 'list' : 'random';
    showToast(isRandom ? '已切换为 列表顺序播放' : '已切换为 随机播放');
  }

  function toggleRepeat() {
    const isSingle = loopMode.value === 'single';
    playerStore.loopMode = isSingle ? 'list' : 'single';
    showToast(isSingle ? '已切换为 列表顺序播放' : '已切换为 单曲循环');
  }

  function cycleLoopMode() {
    const order: LoopMode[] = ['list', 'single', 'random'];
    const cur = order.indexOf(loopMode.value);
    const next = order[(cur + 1) % order.length];
    playerStore.loopMode = next;
    showToast(
      next === 'list' ? '已切换为 列表顺序播放'
        : next === 'single' ? '已切换为 单曲循环'
        : '已切换为 随机播放',
    );
  }

  function toggleLyricView() {
    if (options.activeView() === 'lyric') {
      options.onNavigate('home');
    } else {
      options.onNavigate('lyric');
    }
  }

  /** Open the regular lyric page. No-op when no track is loaded. */
  function openLyricView() {
    if (!currentTrack.value) return;
    if (options.activeView() !== 'lyric') {
      options.onNavigate('lyric');
    }
    setLyricFullscreen(false);
  }

  /** Explicit fullscreen entry. No-op when nothing is loaded. */
  function openLyricImmersion() {
    if (!currentTrack.value) return;
    if (options.activeView() !== 'lyric') {
      const result = options.onNavigate('lyric');
      if (result != null && typeof (result as Promise<unknown>).then === 'function') {
        return Promise.resolve(result).then((ok) => {
          if (ok === false) {
            setLyricFullscreen(false);
            return;
          }
          setLyricFullscreen(true);
        }).catch(() => {
          setLyricFullscreen(false);
        });
      }
      if (result === false) {
        setLyricFullscreen(false);
        return;
      }
    }
    setLyricFullscreen(true);
  }

  function messageForFavoriteResult(result: {
    status: string;
    favorite: boolean;
    error?: string;
  }): string {
    const on = result.favorite;
    switch (result.status) {
      case 'confirmed':
        return on ? '已收藏到「我喜欢的音乐」' : '已取消收藏';
      case 'pending':
        return on ? '已收藏（联网后同步）' : '已取消收藏（联网后同步）';
      case 'local':
        // Outbox write failed (quota / private mode): kept in memory only, NOT
        // in the sync queue - must not claim it will sync.
        return on ? '已收藏到本地（存储不足，未进入同步队列）' : '已取消本地收藏（存储不足，未进入同步队列）';
      case 'anonymous':
        return on ? '已收藏到本地（登录后同步）' : '已取消本地收藏';
      case 'failed':
        return `${on ? '收藏' : '取消收藏'}失败：${result.error || '未知错误'}`;
      default:
        return '';
    }
  }

  async function handleFavorite() {
    const track = currentTrack.value;
    if (!track) return;
    // The heart is the explicit favorite toggle: add/remove the track from
    //「我喜欢的音乐」via the shared favoriteStore (optimistic, operation-id
    // guarded; offline ops land in the outbox). This does NOT open the
    // add-to-playlist modal - that stays available from the search page. The
    // message reflects the actual result (confirmed / pending / anonymous /
    // failed) rather than claiming success prematurely.
    const nextFav = !isFavorite.value;
    if (favToastTimer) clearTimeout(favToastTimer);
    favoriteMsg.value = nextFav ? '收藏中…' : '取消中…';
    const result = await favoriteStore.setFavorite(track, nextFav);
    favoriteMsg.value = messageForFavoriteResult(result);
    favToastTimer = setTimeout(() => { favoriteMsg.value = ''; favToastTimer = null; }, 2000);
  }

  function handleSelectQuality(q: string) {
    if (quality.value === q) {
      showQualityMenu.value = false;
      return;
    }
    showQualityMenu.value = false;
    // Fire-and-forget; setQuality only commits on success and surfaces errors on the bar.
    void Promise.resolve(storeSetQuality(q)).catch((e) => {
      console.error('Quality switch failed', e);
    });
  }

  function closeQualityMenu() {
    showQualityMenu.value = false;
  }

  function closeAddModal() {
    showAddModal.value = false;
  }

  function handleFavoriteSuccess(playlistName: string) {
    const hash = currentTrack.value?.FileHash;
    if (hash) markFavorite(hash);
    favoriteMsg.value = `已收藏到「${playlistName}」`;
    if (favToastTimer) clearTimeout(favToastTimer);
    favToastTimer = setTimeout(() => { favoriteMsg.value = ''; favToastTimer = null; }, 2000);
  }

  function handleFavoriteError(msg: string) {
    favoriteMsg.value = msg;
    if (favToastTimer) clearTimeout(favToastTimer);
    favToastTimer = setTimeout(() => { favoriteMsg.value = ''; favToastTimer = null; }, 2000);
  }

  function getQualityLabel(q: string): string {
    return qualityLabels[q] || q;
  }

  function isCurrentQuality(q: string): boolean {
    return quality.value === q;
  }

  return reactive({
    currentTrack,
    isPlaying,
    isLoading,
    showPauseIcon,
    currentTime,
    duration,
    volume,
    loopMode,
    errorMsg,
    isPreview,
    vipRequired,
    quality,
    coverUrl,
    progressPercent,
    volumePercent,
    isLyricView,
    isFavorite,
    showQualityMenu,
    showAddModal,
    toastMsg,
    favoriteMsg,
    qualityOptions,
    togglePlay,
    next,
    prev,
    seek,
    setVolume,
    setQuality,
    toggleShuffle,
    toggleRepeat,
    cycleLoopMode,
    toggleLyricView,
    openLyricView,
    openLyricImmersion,
    handleFavorite,
    handleSelectQuality,
    closeQualityMenu,
    closeAddModal,
    handleFavoriteSuccess,
    handleFavoriteError,
    getQualityLabel,
    isCurrentQuality,
  }) as PlayerController;
}

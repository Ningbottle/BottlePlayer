import { computed, ref, reactive } from 'vue';
import {
  playerStore,
  togglePlay as storeTogglePlay,
  next as storeNext,
  prev as storePrev,
  seek as storeSeek,
  setVolume as storeSetVolume,
  setQuality as storeSetQuality,
} from '../../api/playerStore';
import type { Track } from '../../api/normalizer';
import type { LoopMode } from '../../api/playerStore';

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
  toggleLyricView: () => void;
  handleFavorite: () => void;
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
  onNavigate: (view: string) => void;
}

const FALLBACK_COVER =
  'data:image/svg+xml;utf8,' + encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56">` +
    `<rect width="56" height="56" fill="#2a2520"/>` +
    `<text x="28" y="34" text-anchor="middle" font-family="Noto Serif SC,serif" ` +
    `font-weight="700" font-size="14" fill="#f1ead8">听</text></svg>`
  );

const qualityLabels: Record<string, string> = {
  '128': '标准',
  '320': '高品',
  'flac': '无损',
  'hires': 'Hi-Res',
  'master': '臻品',
};

const qualityOptions = ['128', '320', 'flac'];

export function usePlayerControls(options: UsePlayerControlsOptions): PlayerController {
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

  const coverUrl = computed(() => currentTrack.value?.Image || FALLBACK_COVER);

  const progressPercent = computed(() => {
    if (!duration.value || duration.value <= 0) return 0;
    return Math.max(0, Math.min(100, (currentTime.value / duration.value) * 100));
  });

  const volumePercent = computed(() => volume.value * 100);

  const showQualityMenu = ref(false);
  const showAddModal = ref(false);
  const toastMsg = ref('');
  const favoriteMsg = ref('');

  let toastTimer: ReturnType<typeof setTimeout> | null = null;
  let favToastTimer: ReturnType<typeof setTimeout> | null = null;

  function showToast(msg: string) {
    toastMsg.value = msg;
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { toastMsg.value = ''; }, 2000);
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

  function toggleLyricView() {
    if (options.activeView() === 'lyric') {
      options.onNavigate('home');
    } else {
      options.onNavigate('lyric');
    }
  }

  function handleFavorite() {
    if (!currentTrack.value) return;
    showAddModal.value = true;
  }

  function handleSelectQuality(q: string) {
    if (quality.value === q) return;
    storeSetQuality(q);
    showQualityMenu.value = false;
  }

  function closeQualityMenu() {
    showQualityMenu.value = false;
  }

  function closeAddModal() {
    showAddModal.value = false;
  }

  function handleFavoriteSuccess(playlistName: string) {
    favoriteMsg.value = `已收藏到「${playlistName}」`;
    if (favToastTimer) clearTimeout(favToastTimer);
    favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
  }

  function handleFavoriteError(msg: string) {
    favoriteMsg.value = msg;
    if (favToastTimer) clearTimeout(favToastTimer);
    favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
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
    toggleLyricView,
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

import { computed, type ComputedRef } from 'vue';
import {
  useHomeFeedStore,
  type HomeSection,
  type HomeSectionState,
  type PlaylistInfo,
} from '../../api/homeFeedStore';
import { playerStore } from '../../api/playerStore';
import type { Track } from '../../api/normalizer';

export interface HomeSectionError {
  section: string;
  message: string;
}

export interface HomeSectionViewState {
  loading: boolean;
  refreshing: boolean;
  error: string | null;
  isEmpty: boolean;
  retry: () => Promise<void>;
}

export interface HomeViewModel {
  heroTrack: Track | null;
  dailyTracks: readonly Track[];
  playlists: readonly PlaylistInfo[];
  albums: readonly PlaylistInfo[];
  queuePreview: readonly Track[];
  /** Zero-based offset of queuePreview within the full playback queue. */
  queueWindowStart: number;
  queueTotal: number;
  /** When personalFm, the home rail should follow the live queue (auto-appends). */
  queueMode: 'normal' | 'personalFm';
  activeQueueHash: string | null;
  isPlaying: boolean;
  isInitialLoading: boolean;
  isRefreshing: boolean;
  sections: Readonly<Record<HomeSection, HomeSectionViewState>>;
  errors: readonly HomeSectionError[];
  /** Named column summary, e.g. "每日推荐、专辑加载失败". Empty when no errors. */
  errorSummary: string;
  /**
   * Real quality/VIP chips for the hero only when hero is the current track.
   * Never includes decorative sample-rate or hard-coded VIP/lossless badges.
   */
  heroQualityChips: readonly string[];
}

const QUEUE_WINDOW_SIZE = 12;

export const HOME_SECTION_LABELS: Record<string, string> = {
  daily: '每日推荐',
  playlists: '编辑推荐',
  albums: '最新歌单',
};

const QUALITY_LABELS: Record<string, string> = {
  '128': '标准',
  '320': '高品',
  flac: '无损',
  hires: 'Hi-Res',
  master: '臻品',
};

function collectErrors(homeFeed: ReturnType<typeof useHomeFeedStore>): HomeSectionError[] {
  const errors: HomeSectionError[] = [];
  if (homeFeed.daily.error) errors.push({ section: 'daily', message: homeFeed.daily.error });
  if (homeFeed.playlists.error) errors.push({ section: 'playlists', message: homeFeed.playlists.error });
  if (homeFeed.albums.error) errors.push({ section: 'albums', message: homeFeed.albums.error });
  return errors;
}

function createSectionViewState<T>(
  section: HomeSection,
  state: HomeSectionState<T>,
  retrySection: (section: HomeSection) => Promise<void>,
): HomeSectionViewState {
  return {
    loading: state.loading,
    refreshing: state.refreshing,
    error: state.error,
    isEmpty: state.loaded && state.items.length === 0,
    retry: () => retrySection(section),
  };
}

export function formatHomeErrorSummary(errors: readonly HomeSectionError[]): string {
  if (!errors.length) return '';
  const names = errors.map((e) => HOME_SECTION_LABELS[e.section] || e.section);
  return `${names.join('、')}加载失败`;
}

export function buildHeroQualityChips(
  heroTrack: Track | null,
  currentTrack: Track | null,
  quality: string | undefined | null,
  vipRequired: boolean,
): string[] {
  if (!heroTrack || !currentTrack || heroTrack.FileHash !== currentTrack.FileHash) {
    return [];
  }
  const chips: string[] = [];
  const q = (quality || '').trim();
  if (q) {
    chips.push(QUALITY_LABELS[q] || q);
  }
  if (vipRequired) {
    chips.push('VIP');
  }
  return chips;
}

function getQueueWindowStart(queue: readonly Track[], currentIndex: number, currentTrack: Track | null): number {
  if (!queue.length) return 0;

  let activeIndex = currentIndex;
  const indexedTrack = queue[activeIndex];
  if (!indexedTrack || indexedTrack.FileHash !== currentTrack?.FileHash) {
    activeIndex = currentTrack
      ? queue.findIndex((track) => track.FileHash === currentTrack.FileHash)
      : -1;
  }

  if (activeIndex < 0) activeIndex = 0;
  const maxStart = Math.max(0, queue.length - QUEUE_WINDOW_SIZE);
  return Math.min(Math.max(activeIndex - Math.floor(QUEUE_WINDOW_SIZE / 2), 0), maxStart);
}

export function useHomeViewModel(): ComputedRef<HomeViewModel> {
  const homeFeed = useHomeFeedStore();

  return computed<HomeViewModel>(() => {
    const heroTrack = playerStore.currentTrack ?? homeFeed.daily.items[0] ?? null;
    const errors = collectErrors(homeFeed);
    const sections = {
      daily: createSectionViewState('daily', homeFeed.daily, homeFeed.retrySection),
      playlists: createSectionViewState('playlists', homeFeed.playlists, homeFeed.retrySection),
      albums: createSectionViewState('albums', homeFeed.albums, homeFeed.retrySection),
    };
    const queueWindowStart = getQueueWindowStart(
      playerStore.queue,
      playerStore.currentIndex,
      playerStore.currentTrack,
    );
    return {
      heroTrack,
      dailyTracks: homeFeed.daily.items,
      playlists: homeFeed.playlists.items,
      albums: homeFeed.albums.items,
      queuePreview: playerStore.queue.slice(queueWindowStart, queueWindowStart + QUEUE_WINDOW_SIZE),
      queueWindowStart,
      queueTotal: playerStore.queue.length,
      queueMode: playerStore.queueMode === 'personalFm' ? 'personalFm' : 'normal',
      activeQueueHash: playerStore.currentTrack?.FileHash ?? null,
      isPlaying: playerStore.isPlaying,
      isInitialLoading:
        (!homeFeed.daily.loaded && homeFeed.daily.loading) ||
        (!homeFeed.playlists.loaded && homeFeed.playlists.loading) ||
        (!homeFeed.albums.loaded && homeFeed.albums.loading),
      isRefreshing:
        homeFeed.daily.refreshing ||
        homeFeed.playlists.refreshing ||
        homeFeed.albums.refreshing,
      sections,
      errors,
      errorSummary: formatHomeErrorSummary(errors),
      heroQualityChips: buildHeroQualityChips(
        heroTrack,
        playerStore.currentTrack,
        playerStore.quality,
        playerStore.vipRequired,
      ),
    };
  });
}

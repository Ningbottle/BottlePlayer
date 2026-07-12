import { computed, type ComputedRef } from 'vue';
import { useHomeFeedStore, type PlaylistInfo } from '../../api/homeFeedStore';
import { playerStore } from '../../api/playerStore';
import type { Track } from '../../api/normalizer';

export interface HomeSectionError {
  section: string;
  message: string;
}

export interface HomeViewModel {
  heroTrack: Track | null;
  dailyTracks: readonly Track[];
  playlists: readonly PlaylistInfo[];
  albums: readonly PlaylistInfo[];
  queuePreview: readonly Track[];
  queueTotal: number;
  activeQueueHash: string | null;
  isPlaying: boolean;
  isInitialLoading: boolean;
  isRefreshing: boolean;
  errors: readonly HomeSectionError[];
  /** Named column summary, e.g. "每日推荐、专辑加载失败". Empty when no errors. */
  errorSummary: string;
  /**
   * Real quality/VIP chips for the hero only when hero is the current track.
   * Never includes decorative sample-rate or hard-coded VIP/lossless badges.
   */
  heroQualityChips: readonly string[];
}

const QUEUE_PREVIEW_COUNT = 12;

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

export function useHomeViewModel(): ComputedRef<HomeViewModel> {
  const homeFeed = useHomeFeedStore();

  return computed<HomeViewModel>(() => {
    const heroTrack = playerStore.currentTrack ?? homeFeed.daily.items[0] ?? null;
    const errors = collectErrors(homeFeed);
    return {
      heroTrack,
      dailyTracks: homeFeed.daily.items,
      playlists: homeFeed.playlists.items,
      albums: homeFeed.albums.items,
      queuePreview: playerStore.queue.slice(0, QUEUE_PREVIEW_COUNT),
      queueTotal: playerStore.queue.length,
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

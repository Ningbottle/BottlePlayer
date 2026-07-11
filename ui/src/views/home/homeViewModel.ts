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
}

const QUEUE_PREVIEW_COUNT = 12;

function collectErrors(homeFeed: ReturnType<typeof useHomeFeedStore>): HomeSectionError[] {
  const errors: HomeSectionError[] = [];
  if (homeFeed.daily.error) errors.push({ section: 'daily', message: homeFeed.daily.error });
  if (homeFeed.playlists.error) errors.push({ section: 'playlists', message: homeFeed.playlists.error });
  if (homeFeed.albums.error) errors.push({ section: 'albums', message: homeFeed.albums.error });
  return errors;
}

export function useHomeViewModel(): ComputedRef<HomeViewModel> {
  const homeFeed = useHomeFeedStore();

  return computed<HomeViewModel>(() => ({
    heroTrack: playerStore.currentTrack ?? homeFeed.daily.items[0] ?? null,
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
    errors: collectErrors(homeFeed),
  }));
}

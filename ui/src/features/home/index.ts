export { default as HomeView } from './HomeView.vue';

export {
  fetchEverydayRecommend,
  fetchTopSong,
  fetchTopPlaylist,
  type FeedResponse,
} from './homeGateway';

export {
  useHomeFeedStore,
  type PlaylistInfo,
  type HomeSectionState,
  type HomeSection,
} from './homeFeedStore';

export {
  nextHomeEnterMode,
  type HomeEnterMode,
} from './homeEnterSession';

export {
  extractDominantColor,
  averagePixels,
  type RGB,
} from './coverColor';

export {
  useHomeViewModel,
  formatHomeErrorSummary,
  buildHeroQualityChips,
  HOME_SECTION_LABELS,
  type HomeViewModel,
  type HomeSectionError,
  type HomeSectionViewState,
} from './homeViewModel';

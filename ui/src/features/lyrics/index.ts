export { default as LyricView } from './LyricView.vue';

export {
  searchLyricCandidates,
  fetchLyricDetail,
  type LyricCandidate,
  type LyricSearchResponse,
  type LyricDetailResponse,
} from './lyricsGateway';

export {
  LyricsResource,
  type LyricLine,
  type LyricsResourceState,
  type LoadLyrics,
} from './lyricsResource';

export {
  useLyricFocusStore,
  type LyricFocusMode,
} from './lyricFocusStore';

export {
  lyricFullscreen,
  setLyricFullscreen,
  clearLyricFullscreenUnlessOnLyric,
} from './lyricFullscreen';

export {
  useLyricFollow,
  IDLE_RESUME_MS,
  type UseLyricFollowOptions,
  type UseLyricFollowReturn,
} from './useLyricFollow';

export {
  useLyricStage,
  parseLrc,
  fetchLyrics,
  type LyricStageModel,
  type LyricStageCommands,
  type UseLyricStageReturn,
} from './useLyricStage';

export {
  useAutoHideControls,
  type AutoHideControls,
  type AutoHideControlsOptions,
} from './useAutoHideControls';

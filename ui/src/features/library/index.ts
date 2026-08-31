export { default as PlaylistView } from './PlaylistView.vue';
export { default as HistoryView } from './HistoryView.vue';
export { default as AddToPlaylistModal } from './AddToPlaylistModal.vue';

export {
  favoriteStore,
  favoriteStateView,
  getUserPlaylists,
  normalizePlaylists,
  isLikedPlaylistName,
  type UserPlaylist,
  type SetFavoriteResult,
} from './favoriteStore';

export {
  addTrackToPlaylist,
  removeTrackFromPlaylist,
} from './favorite';

export {
  isFavoriteMarker,
  markFavorite,
  markFavorites,
  unmarkFavorite,
  reloadFavoriteMarkers,
  favoriteMarkersReadonly,
} from './favoriteMarkers';

export {
  loadLikedPlaylist,
  saveLikedPlaylist,
  clearLikedPlaylist,
  loadOutbox,
  saveOutbox,
  clearOutbox,
  clearUser,
  loadLegacyMarkers,
  clearLegacyMarkers,
  isLegacyMigrated,
  markLegacyMigrated,
  loadAnonymousFavorites,
  saveAnonymousFavorite,
  removeAnonymousFavorite,
  clearAnonymousFavorites,
  type LikedPlaylistInfo,
  type FavoriteOp,
} from './favoriteRepository';

export {
  fetchUserPlaylistsRaw,
  fetchPlaylistTracks,
  type UserPlaylistsResponse,
  type PlaylistTracksResponse,
} from './playlistGateway';

export {
  fetchUserHistory,
  type UserHistoryResponse,
} from './historyGateway';

export {
  addPlaylistTracks,
  removePlaylistTracks,
  type ModifyPlaylistTracksResponse,
} from './favoriteGateway';

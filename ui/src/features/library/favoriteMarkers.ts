import {
  favoriteStore,
  isLikedPlaylistName,
  favoriteStateView,
  __resetFavoriteStoreForTests,
} from './favoriteStore';

/**
 * Compatibility cache projection. favoriteStore is now the authority for
 * favorite state (isFavorite / setFavorite / reconcile / outbox); this module
 * only re-exposes it under the legacy marker API so existing consumers (player
 * bars, PlaylistView) read the SAME shared state without churn. It is no longer
 * authoritative state of its own.
 */
export { isLikedPlaylistName };

export function isFavoriteMarker(fileHash: string | null | undefined): boolean {
  return favoriteStore.isFavorite(fileHash);
}

export function markFavorite(fileHash: string): void {
  favoriteStore.markFavoriteByHash(fileHash, true);
}

/** Bulk-mark FileHashes (e.g. after loading「我喜欢的音乐」). */
export function markFavorites(fileHashes: readonly string[]): void {
  for (const h of fileHashes) {
    if (h) favoriteStore.markFavoriteByHash(h, true);
  }
}

export function unmarkFavorite(fileHash: string): void {
  favoriteStore.markFavoriteByHash(fileHash, false);
}

/** Reload from storage (e.g. after multi-tab) - favoriteStore is the source. */
export function reloadFavoriteMarkers(): void {
  void favoriteStore.reconcile();
}

/**
 * Reactive view of the favorite state. Reading `.hashes.has(hash)` in a
 * computed tracks favoriteStore's authoritative set.
 */
export function favoriteMarkersReadonly() {
  return favoriteStateView;
}

/** Test-only: reset the authoritative favorite store. */
export function __resetFavoriteMarkersForTests(): void {
  __resetFavoriteStoreForTests();
}

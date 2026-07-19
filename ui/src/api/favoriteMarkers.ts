import { reactive, readonly } from 'vue';

/**
 * Shared in-memory + localStorage favorite markers (by FileHash).
 * Used by player bars so a successful collect lights the heart immediately
 * and survives bar remounts within the session.
 */
const STORAGE_KEY = 'player_favorite_markers';

function loadFromStorage(): Set<string> {
  try {
    const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
    if (!Array.isArray(stored)) return new Set();
    return new Set(stored.filter((v): v is string => typeof v === 'string' && v.length > 0));
  } catch {
    return new Set();
  }
}

function persist(markers: Set<string>): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify([...markers]));
  } catch {
    // quota / private mode — keep in-memory only
  }
}

const state = reactive({
  hashes: loadFromStorage(),
});

export function isFavoriteMarker(fileHash: string | null | undefined): boolean {
  if (!fileHash) return false;
  return state.hashes.has(fileHash);
}

export function markFavorite(fileHash: string): void {
  if (!fileHash) return;
  if (state.hashes.has(fileHash)) return;
  const next = new Set(state.hashes);
  next.add(fileHash);
  state.hashes = next;
  persist(next);
}

/** Bulk-mark FileHashes (e.g. after loading「我喜欢的音乐」). */
export function markFavorites(fileHashes: readonly string[]): void {
  if (!fileHashes.length) return;
  const next = new Set(state.hashes);
  let changed = false;
  for (const hash of fileHashes) {
    if (!hash || next.has(hash)) continue;
    next.add(hash);
    changed = true;
  }
  if (!changed) return;
  state.hashes = next;
  persist(next);
}

/**
 * True for KuGou "liked" playlist names used as the heart source of truth.
 * Match is intentionally loose (云盘/本地命名差异).
 */
export function isLikedPlaylistName(name: string | null | undefined): boolean {
  if (!name) return false;
  const n = name.trim();
  if (!n) return false;
  return /我喜欢|喜欢的音乐|我的最爱|我的收藏|favorites?|liked\s*songs?/i.test(n);
}

export function unmarkFavorite(fileHash: string): void {
  if (!fileHash || !state.hashes.has(fileHash)) return;
  const next = new Set(state.hashes);
  next.delete(fileHash);
  state.hashes = next;
  persist(next);
}

/** Reload from storage (e.g. after multi-tab). */
export function reloadFavoriteMarkers(): void {
  state.hashes = loadFromStorage();
}

export function favoriteMarkersReadonly() {
  return readonly(state);
}

/** Test-only. */
export function __resetFavoriteMarkersForTests(): void {
  state.hashes = new Set();
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch {
    /* ignore */
  }
}

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

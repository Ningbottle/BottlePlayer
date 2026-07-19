import type { Track } from './normalizer';

/**
 * Persistence layer for the favorite domain. All keys are scoped per-user so
 * favorite state, the resolved「我喜欢的音乐」id, and the pending outbox never
 * leak across accounts.
 *
 * This module is intentionally side-effect free at import time (no localStorage
 * access until a function is called) so it is safe to import in any context.
 */

export interface LikedPlaylistInfo {
  /** global_collection_id (collection_…) used to FETCH tracks via /playlist/track/all. */
  gid: string;
  /** Numeric listid used for /playlist/tracks/add and /playlist/tracks/del. */
  listid: string;
  name: string;
}

export interface FavoriteOp {
  opId: number;
  fileHash: string;
  favorite: boolean;
  track: Track;
  ts: number;
}

const LEGACY_MARKER_KEY = 'player_favorite_markers';
const ANON_FAVORITES_KEY = 'bm_fav_anonymous';
const likedKey = (uid: string) => `bm_fav_liked_${uid}`;
const outboxKey = (uid: string) => `bm_fav_outbox_${uid}`;
const legacyMigratedKey = (uid: string) => `bm_fav_legacy_migrated_${uid}`;

function safeParse<T>(raw: string | null, fallback: T): T {
  if (!raw) return fallback;
  try {
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

export function loadLikedPlaylist(uid: string): LikedPlaylistInfo | null {
  if (!uid) return null;
  return safeParse<LikedPlaylistInfo | null>(localStorage.getItem(likedKey(uid)), null);
}

export function saveLikedPlaylist(uid: string, info: LikedPlaylistInfo): void {
  if (!uid) return;
  try {
    localStorage.setItem(likedKey(uid), JSON.stringify(info));
  } catch {
    /* quota / private mode - keep in-memory only */
  }
}

export function clearLikedPlaylist(uid: string): void {
  if (!uid) return;
  try {
    localStorage.removeItem(likedKey(uid));
  } catch {
    /* ignore */
  }
}

export function loadOutbox(uid: string): FavoriteOp[] {
  if (!uid) return [];
  const parsed = safeParse<unknown>(localStorage.getItem(outboxKey(uid)), []);
  return Array.isArray(parsed) ? (parsed as FavoriteOp[]) : [];
}

/**
 * Persist the outbox. Returns false on quota/private-mode failure (the op list
 * is NOT persisted) so callers can avoid dropping a source they're migrating
 * from.
 */
export function saveOutbox(uid: string, ops: FavoriteOp[]): boolean {
  if (!uid) return false;
  try {
    localStorage.setItem(outboxKey(uid), JSON.stringify(ops));
    return true;
  } catch {
    return false; // quota / private mode - keep in-memory only
  }
}

export function clearOutbox(uid: string): void {
  if (!uid) return;
  try {
    localStorage.removeItem(outboxKey(uid));
  } catch {
    /* ignore */
  }
}

/** Clear all persisted favorite state for a user (liked id + outbox). */
export function clearUser(uid: string): void {
  clearLikedPlaylist(uid);
  clearOutbox(uid);
}

/**
 * One-time migration source: the legacy `player_favorite_markers` cache stored a
 * flat FileHash list. It is imported as preliminary favorites until the first
 * reconcile confirms ground truth, then cleared.
 */
export function loadLegacyMarkers(): string[] {
  const parsed = safeParse<unknown>(localStorage.getItem(LEGACY_MARKER_KEY), []);
  if (!Array.isArray(parsed)) return [];
  return parsed.filter((v): v is string => typeof v === 'string' && v.length > 0);
}

export function clearLegacyMarkers(): void {
  try {
    localStorage.removeItem(LEGACY_MARKER_KEY);
  } catch {
    /* ignore */
  }
}

export function isLegacyMigrated(uid: string): boolean {
  if (!uid) return true;
  return localStorage.getItem(legacyMigratedKey(uid)) === '1';
}

export function markLegacyMigrated(uid: string): void {
  if (!uid) return;
  try {
    localStorage.setItem(legacyMigratedKey(uid), '1');
  } catch {
    /* ignore */
  }
}

/**
 * Anonymous favorites (created while logged out). Stored as full tracks so they
 * can be replayed to the liked playlist after login. Keyed globally (not per-
 * user) since there is no bound user.
 */
export function loadAnonymousFavorites(): Track[] {
  const parsed = safeParse<unknown>(localStorage.getItem(ANON_FAVORITES_KEY), []);
  if (!Array.isArray(parsed)) return [];
  return parsed.filter((v): v is Track => !!v && typeof v === 'object' && !!(v as Track).FileHash);
}

export function saveAnonymousFavorite(track: Track): void {
  if (!track?.FileHash) return;
  const list = loadAnonymousFavorites().filter((t) => t.FileHash !== track.FileHash);
  list.push(track);
  try {
    localStorage.setItem(ANON_FAVORITES_KEY, JSON.stringify(list));
  } catch {
    /* quota / private mode */
  }
}

export function removeAnonymousFavorite(fileHash: string): void {
  if (!fileHash) return;
  const list = loadAnonymousFavorites().filter((t) => t.FileHash !== fileHash);
  try {
    localStorage.setItem(ANON_FAVORITES_KEY, JSON.stringify(list));
  } catch {
    /* quota / private mode */
  }
}

export function clearAnonymousFavorites(): void {
  try {
    localStorage.removeItem(ANON_FAVORITES_KEY);
  } catch {
    /* ignore */
  }
}

/** Test-only: drop every favorite-related key from localStorage. */
export function __resetFavoriteRepositoryForTests(): void {
  try {
    const keys: string[] = [];
    for (let i = 0; i < localStorage.length; i++) {
      const k = localStorage.key(i);
      if (k && (k.startsWith('bm_fav_') || k === LEGACY_MARKER_KEY)) keys.push(k);
    }
    keys.forEach((k) => localStorage.removeItem(k));
  } catch {
    /* ignore */
  }
}

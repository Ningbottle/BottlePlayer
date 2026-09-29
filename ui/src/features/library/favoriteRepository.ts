import type { Track } from '../../shared/music/track';
import { getLocalStorage, safeGetItem, safeRemoveItem, safeSetItem } from '../../platform/storage/safeStorage';

/**
 * Persistence layer for the favorite domain. All keys are scoped per-user so
 * favorite state, the resolved「我喜欢的音乐」id, and the pending outbox never
 * leak across accounts.
 *
 * This module is intentionally side-effect free at import time and every access
 * goes through safeStorage: the `localStorage` property itself throws
 * SecurityError on hosts with site data blocked, and an unguarded `getItem`
 * used to propagate out of `loadLikedPlaylist` / `loadOutbox` and look like a
 * crash (or, worse, an empty favorite list) instead of a storage miss.
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
  return asLikedPlaylist(safeParse<unknown>(safeGetItem(likedKey(uid)), null));
}

function asLikedPlaylist(value: unknown): LikedPlaylistInfo | null {
  if (!value || typeof value !== 'object') return null;
  const info = value as Partial<LikedPlaylistInfo>;
  if (typeof info.gid !== 'string' || !info.gid.trim() || typeof info.listid !== 'string' || !info.listid.trim()) {
    return null;
  }
  return { gid: info.gid, listid: info.listid, name: typeof info.name === 'string' ? info.name : '' };
}

export function saveLikedPlaylist(uid: string, info: LikedPlaylistInfo): void {
  if (!uid) return;
  safeSetItem(likedKey(uid), JSON.stringify(info));
}

export function clearLikedPlaylist(uid: string): void {
  if (!uid) return;
  safeRemoveItem(likedKey(uid));
}

export function loadOutbox(uid: string): FavoriteOp[] {
  if (!uid) return [];
  const parsed = safeParse<unknown>(safeGetItem(outboxKey(uid)), []);
  return Array.isArray(parsed) ? parsed.filter(isFavoriteOp) : [];
}

/**
 * An outbox entry is replayed against the remote playlist, so a corrupted or
 * hand-edited record must not reach the API: only complete ops with a usable
 * identity (opId, fileHash, track identity, timestamp) survive the load.
 */
function isFavoriteOp(value: unknown): value is FavoriteOp {
  if (!value || typeof value !== 'object') return false;
  const op = value as Partial<FavoriteOp>;
  return typeof op.opId === 'number' && Number.isFinite(op.opId)
    && typeof op.fileHash === 'string' && op.fileHash.trim().length > 0
    && typeof op.favorite === 'boolean'
    && !!op.track && typeof op.track === 'object'
    && typeof (op.track as Track).FileHash === 'string' && !!((op.track as Track).FileHash || '').trim()
    && typeof op.ts === 'number' && Number.isFinite(op.ts);
}

/**
 * Persist the outbox. Returns false on quota/private-mode failure (the op list
 * is NOT persisted) so callers can avoid dropping a source they're migrating
 * from.
 */
export function saveOutbox(uid: string, ops: FavoriteOp[]): boolean {
  if (!uid) return false;
  return safeSetItem(outboxKey(uid), JSON.stringify(ops));
}

export function clearOutbox(uid: string): void {
  if (!uid) return;
  safeRemoveItem(outboxKey(uid));
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
  const parsed = safeParse<unknown>(safeGetItem(LEGACY_MARKER_KEY), []);
  if (!Array.isArray(parsed)) return [];
  return parsed.filter((v): v is string => typeof v === 'string' && v.length > 0);
}

export function clearLegacyMarkers(): void {
  safeRemoveItem(LEGACY_MARKER_KEY);
}

export function isLegacyMigrated(uid: string): boolean {
  if (!uid) return true;
  return safeGetItem(legacyMigratedKey(uid)) === '1';
}

export function markLegacyMigrated(uid: string): void {
  if (!uid) return;
  safeSetItem(legacyMigratedKey(uid), '1');
}

/**
 * Anonymous favorites (created while logged out). Stored as full tracks so they
 * can be replayed to the liked playlist after login. Keyed globally (not per-
 * user) since there is no bound user.
 */
export function loadAnonymousFavorites(): Track[] {
  const parsed = safeParse<unknown>(safeGetItem(ANON_FAVORITES_KEY), []);
  if (!Array.isArray(parsed)) return [];
  return parsed.filter((v): v is Track => !!v && typeof v === 'object' && !!(v as Track).FileHash);
}

export function saveAnonymousFavorite(track: Track): void {
  if (!track?.FileHash) return;
  const list = loadAnonymousFavorites().filter((t) => t.FileHash !== track.FileHash);
  list.push(track);
  safeSetItem(ANON_FAVORITES_KEY, JSON.stringify(list));
}

export function removeAnonymousFavorite(fileHash: string): void {
  if (!fileHash) return;
  const list = loadAnonymousFavorites().filter((t) => t.FileHash !== fileHash);
  safeSetItem(ANON_FAVORITES_KEY, JSON.stringify(list));
}

export function clearAnonymousFavorites(): void {
  safeRemoveItem(ANON_FAVORITES_KEY);
}

/** Test-only: drop every favorite-related key from localStorage. */
export function __resetFavoriteRepositoryForTests(): void {
  const store = getLocalStorage();
  if (!store) return;
  const keys: string[] = [];
  try {
    for (let i = 0; i < store.length; i++) {
      const k = store.key(i);
      if (k && (k.startsWith('bm_fav_') || k === LEGACY_MARKER_KEY)) keys.push(k);
    }
  } catch {
    /* enumeration unavailable: nothing to reset */
    return;
  }
  keys.forEach((k) => safeRemoveItem(k));
}

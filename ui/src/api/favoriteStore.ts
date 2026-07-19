import { reactive, readonly } from 'vue';
import { apiGet } from './backend';
import { normalizeTrack, type Track } from './normalizer';
import {
  addTrackToPlaylist,
  removeTrackFromPlaylist,
  type UserPlaylist,
} from './favorite';

export type { UserPlaylist };
import {
  loadLikedPlaylist,
  saveLikedPlaylist,
  loadOutbox,
  saveOutbox,
  loadLegacyMarkers,
  clearLegacyMarkers,
  isLegacyMigrated,
  markLegacyMigrated,
  type LikedPlaylistInfo,
  type FavoriteOp,
} from './favoriteRepository';

// ── Playlist discovery (moved here from favorite.ts; favorite.ts is add/del only) ──

const PLAYLIST_ARRAY_KEYS = [
  'list', 'lists', 'info', 'special_list', 'specialList',
  'cloud_list', 'cloudList', 'playlist', 'playlists', 'data',
];

const PLAYLIST_ID_KEYS = [
  'global_collection_id', 'global_collectionid', 'listid', 'list_id',
  'specialid', 'special_id', 'id', 'list_create_listid', 'collection_id', 'gid',
];

const PLAYLIST_NAME_KEYS = [
  'name', 'listname', 'list_name', 'specialname', 'special_name', 'title', 'filename',
];

type RawRecord = Record<string, unknown>;

function asRecord(value: unknown): RawRecord | null {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as RawRecord : null;
}

function readField(source: RawRecord, keys: string[]): string {
  for (const key of keys) {
    const value = source[key];
    if (typeof value === 'string' && value.trim()) return value.trim();
    if (typeof value === 'number' && Number.isFinite(value)) return String(value);
    if (typeof value === 'bigint') return value.toString();
  }
  return '';
}

function looksLikePlaylist(value: unknown): boolean {
  const record = asRecord(value);
  if (!record) return false;
  return !!readField(record, PLAYLIST_ID_KEYS) || !!readField(record, PLAYLIST_NAME_KEYS);
}

function collectPlaylistCandidates(value: unknown, output: RawRecord[], depth = 0): void {
  if (depth > 5 || value == null) return;

  if (Array.isArray(value)) {
    const records = value.map(asRecord).filter((item): item is RawRecord => !!item);
    if (records.some(looksLikePlaylist)) {
      output.push(...records.filter(looksLikePlaylist));
      return;
    }
    records.forEach((item) => collectPlaylistCandidates(item, output, depth + 1));
    return;
  }

  const record = asRecord(value);
  if (!record) return;

  if (looksLikePlaylist(record)) {
    output.push(record);
  }

  for (const key of PLAYLIST_ARRAY_KEYS) {
    if (key in record) {
      collectPlaylistCandidates(record[key], output, depth + 1);
    }
  }
}

/** 从 API 响应中提取并规范化歌单列表 */
export function normalizePlaylists(payload: unknown): UserPlaylist[] {
  const candidates: RawRecord[] = [];
  collectPlaylistCandidates(payload, candidates);

  const seen = new Set<string>();
  return candidates
    .map((item) => {
      const id = readField(item, PLAYLIST_ID_KEYS);
      // 优先使用 listid（纯数字），用于 C++ 后端 AddPlaylistTracks / DeletePlaylistTracks
      const listid = readField(item, ['listid', 'list_id']);
      return {
        id,
        name: readField(item, PLAYLIST_NAME_KEYS) || '无标题歌单',
        songcount: Number(item.songcount || item.song_count || 0),
        listid: listid || undefined,
      };
    })
    .filter((item) => {
      if (!item.id || seen.has(item.id)) return false;
      seen.add(item.id);
      return true;
    });
}

/** 获取用户歌单列表 */
export async function getUserPlaylists(): Promise<UserPlaylist[]> {
  try {
    const res = await apiGet<any>('/user/playlist', { page: 1, pagesize: 100 });
    return normalizePlaylists(res);
  } catch (e) {
    console.error('获取歌单列表失败', e);
    return [];
  }
}

/**
 * True for KuGou "liked" playlist names. Used ONLY as the first-migration
 * fallback to locate the「我喜欢的音乐」playlist before its id is persisted;
 * afterwards the persisted numeric listid is authoritative.
 */
export function isLikedPlaylistName(name: string | null | undefined): boolean {
  if (!name) return false;
  const n = name.trim();
  if (!n) return false;
  return /我喜欢|喜欢的音乐|我的最爱|我的收藏|favorites?|liked\s*songs?/i.test(n);
}

// ── Liked-playlist track fetching ──

interface TracksPage {
  tracks: Track[];
  total: number;
}

async function fetchLikedTracksPage(
  gid: string,
  page: number,
  pageSize: number,
): Promise<TracksPage> {
  const res = await apiGet<{ status: number; error?: string; data?: { list?: unknown[]; total?: number } }>(
    '/playlist/track/all',
    { id: gid, page, pagesize: pageSize },
  );
  if (!res || res.status !== 1 || !res.data) return { tracks: [], total: 0 };
  const list = Array.isArray(res.data.list) ? res.data.list : [];
  return { tracks: list.map(normalizeTrack), total: res.data.total ?? list.length };
}

async function fetchAllLikedTracks(gid: string): Promise<Track[]> {
  const pageSize = 50;
  let page = 1;
  let total = 0;
  const all: Track[] = [];
  do {
    const { tracks, total: t } = await fetchLikedTracksPage(gid, page, pageSize);
    all.push(...tracks);
    total = t;
    page += 1;
    // Safety cap: never page past 50 pages even if total is huge/wrong.
  } while (all.length < total && page <= 50);
  return all;
}

// ── Authoritative favorite state ──

interface FavoriteState {
  /** Authoritative set of favorite FileHashes (liked-playlist membership). */
  hashes: Set<string>;
  loaded: boolean;
  reconciling: boolean;
  lastError: string | null;
  pendingOutbox: number;
}

const state = reactive<FavoriteState>({
  hashes: new Set<string>(),
  loaded: false,
  reconciling: false,
  lastError: null,
  pendingOutbox: 0,
});

let boundUserId = '';
let likedPlaylist: LikedPlaylistInfo | null = null;
/** FileHash -> Track archive for del (fileid lookup) and outbox replay. */
const trackArchive = new Map<string, Track>();
let opCounter = 0;
/** Per-hash latest operation id; an older response must not relight the heart. */
const lastOpId = new Map<string, number>();
let reconcileInFlight: Promise<void> | null = null;

function applyFavorite(hash: string, favorite: boolean): void {
  if (favorite) state.hashes.add(hash);
  else state.hashes.delete(hash);
}

function resetInMemory(): void {
  state.hashes.clear();
  state.loaded = false;
  state.reconciling = false;
  state.lastError = null;
  state.pendingOutbox = 0;
  likedPlaylist = null;
  trackArchive.clear();
  lastOpId.clear();
  opCounter = 0;
  reconcileInFlight = null;
}

async function resolveLikedPlaylist(): Promise<LikedPlaylistInfo | null> {
  if (likedPlaylist) return likedPlaylist;
  if (boundUserId) {
    const persisted = loadLikedPlaylist(boundUserId);
    if (persisted) {
      likedPlaylist = persisted;
      return likedPlaylist;
    }
  }
  // First-migration fallback: locate the liked playlist by name regex.
  const playlists = await getUserPlaylists();
  const liked = playlists.find((p) => isLikedPlaylistName(p.name));
  if (liked && liked.listid) {
    const info: LikedPlaylistInfo = { gid: liked.id, listid: liked.listid, name: liked.name };
    likedPlaylist = info;
    if (boundUserId) saveLikedPlaylist(boundUserId, info);
    return info;
  }
  return null;
}

async function reconcile(): Promise<void> {
  if (!boundUserId) return;
  if (reconcileInFlight) return reconcileInFlight;
  state.reconciling = true;
  state.lastError = null;
  reconcileInFlight = (async () => {
    try {
      const liked = await resolveLikedPlaylist();
      if (!liked) {
        state.hashes.clear();
        trackArchive.clear();
        state.loaded = true;
        return;
      }
      const tracks = await fetchAllLikedTracks(liked.gid);
      // Rebuild from ground truth.
      state.hashes.clear();
      trackArchive.clear();
      for (const t of tracks) {
        if (t.FileHash) {
          state.hashes.add(t.FileHash);
          trackArchive.set(t.FileHash, t);
        }
      }
      state.loaded = true;
    } catch (e) {
      state.lastError = e instanceof Error ? e.message : String(e);
    } finally {
      state.reconciling = false;
      reconcileInFlight = null;
    }
  })();
  return reconcileInFlight;
}

function outboxCount(): number {
  return boundUserId ? loadOutbox(boundUserId).length : 0;
}

function refreshOutboxCount(): void {
  state.pendingOutbox = outboxCount();
}

function enqueueOutboxOp(op: FavoriteOp): void {
  if (!boundUserId) return;
  const ops = loadOutbox(boundUserId);
  ops.push(op);
  saveOutbox(boundUserId, ops);
  refreshOutboxCount();
}

function dequeueOutboxOp(hash: string, opId: number): void {
  if (!boundUserId) return;
  const ops = loadOutbox(boundUserId).filter((o) => !(o.fileHash === hash && o.opId === opId));
  saveOutbox(boundUserId, ops);
  refreshOutboxCount();
}

/**
 * Replay pending offline operations (compacted to the latest op per hash = net
 * intent). Transport failures keep the op for the next replay; business
 * responses (success or status !== 1) drop the op since the server has
 * authoritative state.
 */
async function flushOutbox(): Promise<void> {
  if (!boundUserId) return;
  const ops = loadOutbox(boundUserId);
  if (ops.length === 0) {
    refreshOutboxCount();
    return;
  }
  const liked = await resolveLikedPlaylist();
  if (!liked) return; // can't replay without the liked listid
  const playlist: UserPlaylist = { id: liked.gid, listid: liked.listid, name: liked.name };

  const latestByHash = new Map<string, FavoriteOp>();
  for (const op of ops) latestByHash.set(op.fileHash, op);

  const failed: FavoriteOp[] = [];
  for (const op of latestByHash.values()) {
    try {
      const res = op.favorite
        ? await addTrackToPlaylist(playlist, op.track)
        : await removeTrackFromPlaylist(playlist, op.track);
      if (!res.success) {
        // Business failure on replay: drop the op; reconcile will re-sync.
        void reconcile();
      }
    } catch {
      // Transport error: keep for the next replay.
      failed.push(op);
    }
  }
  saveOutbox(boundUserId, failed);
  refreshOutboxCount();
}

async function sync(): Promise<void> {
  // Flush pending offline ops FIRST so the server reflects them before we
  // re-fetch ground truth.
  await flushOutbox();
  await reconcile();
}

async function setFavorite(track: Track, favorite: boolean): Promise<void> {
  const hash = track?.FileHash;
  if (!hash) return;

  const opId = ++opCounter;
  lastOpId.set(hash, opId);
  trackArchive.set(hash, track);
  applyFavorite(hash, favorite); // optimistic, before any await

  if (!boundUserId) return; // not logged in: in-memory only

  const liked = await resolveLikedPlaylist();
  if (!liked) return; // no liked playlist to target

  const playlist: UserPlaylist = { id: liked.gid, listid: liked.listid, name: liked.name };
  try {
    const res = favorite
      ? await addTrackToPlaylist(playlist, track)
      : await removeTrackFromPlaylist(playlist, track);

    // Only the LATEST op for this hash may confirm state.
    if (lastOpId.get(hash) !== opId) {
      // Stale: a newer op supersedes this. Never relight from an old response.
      dequeueOutboxOp(hash, opId);
      return;
    }
    dequeueOutboxOp(hash, opId);
    if (!res.success) {
      // Business failure on the latest op: re-sync from the server.
      void reconcile();
    }
  } catch {
    // Transport error (offline / circuit open): persist for replay, but only if
    // this is still the latest op for the hash (a newer op owns the state).
    if (lastOpId.get(hash) === opId) {
      enqueueOutboxOp({ opId, fileHash: hash, favorite, track, ts: Date.now() });
    }
  }
}

function markFavoriteByHash(hash: string, favorite: boolean): void {
  if (!hash) return;
  applyFavorite(hash, favorite);
}

function hydrateLikedPage(tracks: Track[]): void {
  for (const t of tracks) {
    if (t.FileHash) {
      state.hashes.add(t.FileHash);
      trackArchive.set(t.FileHash, t);
    }
  }
}

async function onLogin(userId: string): Promise<void> {
  if (!userId) return;
  if (boundUserId && boundUserId !== userId) {
    resetInMemory();
  }
  boundUserId = userId;

  // One-time legacy marker migration: import the old flat cache as preliminary
  // favorites until reconcile confirms ground truth, then clear it.
  if (!isLegacyMigrated(userId)) {
    for (const h of loadLegacyMarkers()) applyFavorite(h, true);
    markLegacyMigrated(userId);
    clearLegacyMarkers();
  }

  refreshOutboxCount();
  await sync();
}

function onLogout(): void {
  // Keep persisted outbox/liked for the user (they may log back in); only clear
  // in-memory state.
  resetInMemory();
  boundUserId = '';
}

async function onOnline(): Promise<void> {
  if (!boundUserId) return;
  await sync();
}

export const favoriteStore = {
  isFavorite(hash: string | null | undefined): boolean {
    return !!hash && state.hashes.has(hash);
  },
  get loaded(): boolean {
    return state.loaded;
  },
  get reconciling(): boolean {
    return state.reconciling;
  },
  get pendingOutbox(): number {
    return state.pendingOutbox;
  },
  get lastError(): string | null {
    return state.lastError;
  },
  getLikedPlaylist(): { gid: string; listid: string } | null {
    return likedPlaylist ? { gid: likedPlaylist.gid, listid: likedPlaylist.listid } : null;
  },
  setFavorite(track: Track, favorite: boolean): Promise<void> {
    return setFavorite(track, favorite);
  },
  markFavoriteByHash(hash: string, favorite: boolean): void {
    markFavoriteByHash(hash, favorite);
  },
  hydrateLikedPage(tracks: Track[]): void {
    hydrateLikedPage(tracks);
  },
  reconcile(): Promise<void> {
    return reconcile();
  },
  flushOutbox(): Promise<void> {
    return flushOutbox();
  },
  onLogin(userId: string): Promise<void> {
    return onLogin(userId);
  },
  onLogout(): void {
    onLogout();
  },
  onOnline(): Promise<void> {
    return onOnline();
  },
};

// ── Auto-hooks: network recovery (browser online + tab visibility) ──
// Login/startup hooks are fired from userStore (after checkLoginStatus). App.vue
// is not modified, so network recovery is observed via browser events instead
// of the ping() loop.

if (typeof window !== 'undefined') {
  window.addEventListener('online', () => {
    void favoriteStore.onOnline();
  });
  let lastVisibleSync = 0;
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      const now = Date.now();
      if (now - lastVisibleSync > 30_000) {
        lastVisibleSync = now;
        void favoriteStore.onOnline();
      }
    }
  });
}

/** Test-only: reset all in-memory state (does not touch persisted storage). */
export function __resetFavoriteStoreForTests(): void {
  resetInMemory();
}

/** Reactive readonly view of the favorite state (for compat projections). */
export const favoriteStateView = readonly(state);

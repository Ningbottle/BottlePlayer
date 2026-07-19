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
  loadAnonymousFavorites,
  saveAnonymousFavorite,
  removeAnonymousFavorite,
  clearAnonymousFavorites,
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

/**
 * Fetch the user's playlists. THROWS on network/transport errors so callers
 * (resolveLikedPlaylist) can distinguish "unavailable" from "no liked playlist".
 */
async function fetchUserPlaylists(): Promise<UserPlaylist[]> {
  const res = await apiGet<any>('/user/playlist', { page: 1, pagesize: 100 });
  return normalizePlaylists(res);
}

/** 获取用户歌单列表（对外：网络异常吞为空数组，供 AddToPlaylistModal 等 UI 使用）。 */
export async function getUserPlaylists(): Promise<UserPlaylist[]> {
  try {
    return await fetchUserPlaylists();
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

/**
 * Outcome of a setFavorite call. The UI must surface these distinctly instead
 * of claiming success before the result is known.
 * - confirmed: the server accepted the add/del.
 * - pending: transport error; the op is queued in the outbox for replay.
 * - anonymous: not logged in; saved to a local anonymous store (migrated on login).
 * - failed: business failure (server responded with status !== 1, or no liked
 *   playlist to target); the optimistic state is rolled back.
 */
export type SetFavoriteResult =
  | { status: 'confirmed'; favorite: boolean }
  | { status: 'pending'; favorite: boolean }
  | { status: 'anonymous'; favorite: boolean }
  | { status: 'failed'; favorite: boolean; error: string };

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
/**
 * Account epoch: bumped on every onLogin/onLogout. Long-running sync ops
 * capture it at start and verify it before applying any state/repo write, so a
 * mid-flight account switch can never pollute the new user's favorites or
 * persisted keys.
 */
let accountEpoch = 0;
let likedPlaylist: LikedPlaylistInfo | null = null;
/** FileHash -> Track archive for del (fileid lookup) and outbox replay. */
const trackArchive = new Map<string, Track>();
let opCounter = 0;
/** Per-hash latest operation id; an older response must not relight the heart. */
const lastOpId = new Map<string, number>();
/**
 * Per-hash intended favorite state for ops not yet reflected in a server
 * snapshot we've seen (in-flight API call, queued outbox op, or
 * confirmed-but-not-yet-reconciled). reconcile re-applies this on top of the
 * server snapshot so a stale fetch can't clobber a concurrent setFavorite, then
 * clears entries the server agrees with.
 */
const pendingIntent = new Map<string, boolean>();
let reconcileInFlight: Promise<void> | null = null;

function applyFavorite(hash: string, favorite: boolean): void {
  if (favorite) state.hashes.add(hash);
  else state.hashes.delete(hash);
}

/**
 * Re-apply pending optimistic intent on top of a freshly-fetched server
 * snapshot, then drop entries the server has caught up to. Called only when
 * the account epoch is unchanged.
 */
function reapplyIntent(serverHashes: Set<string>): void {
  for (const [hash, fav] of pendingIntent) {
    if (fav) state.hashes.add(hash);
    else state.hashes.delete(hash);
    if (serverHashes.has(hash) === fav) {
      // Server agrees -> the op is resolved; stop overriding future reconciles.
      pendingIntent.delete(hash);
    }
  }
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
  pendingIntent.clear();
  opCounter = 0;
  reconcileInFlight = null;
  boundUserId = '';
}

async function resolveLikedPlaylist(): Promise<LikedPlaylistInfo | null> {
  if (likedPlaylist) return likedPlaylist;
  const epoch = accountEpoch;
  const userId = boundUserId;
  if (userId) {
    const persisted = loadLikedPlaylist(userId);
    if (persisted) {
      likedPlaylist = persisted;
      return likedPlaylist;
    }
  }
  // First-migration fallback: locate the liked playlist by name regex.
  // fetchUserPlaylists THROWS on network errors (vs returning []), so we can
  // distinguish "offline" from "genuinely no liked playlist".
  const playlists = await fetchUserPlaylists();
  // Account switched while fetching - do not cache/save under the new user.
  if (epoch !== accountEpoch || userId !== boundUserId) return null;
  const liked = playlists.find((p) => isLikedPlaylistName(p.name));
  if (liked && liked.listid) {
    const info: LikedPlaylistInfo = { gid: liked.id, listid: liked.listid, name: liked.name };
    likedPlaylist = info;
    if (userId) saveLikedPlaylist(userId, info);
    return info;
  }
  return null;
}

async function reconcile(): Promise<void> {
  if (!boundUserId) return;
  if (reconcileInFlight) return reconcileInFlight;
  const epoch = accountEpoch;
  state.reconciling = true;
  state.lastError = null;
  const promise = (async () => {
    try {
      const liked = await resolveLikedPlaylist();
      if (epoch !== accountEpoch) return; // account switched mid-fetch
      if (!liked) {
        state.hashes.clear();
        trackArchive.clear();
        reapplyIntent(new Set());
        state.loaded = true;
        return;
      }
      const tracks = await fetchAllLikedTracks(liked.gid);
      if (epoch !== accountEpoch) return; // account switched mid-fetch
      // Rebuild from ground truth, then re-apply any concurrent optimistic
      // intent so a stale snapshot can't unlight a favorite just made.
      state.hashes.clear();
      trackArchive.clear();
      const serverHashes = new Set<string>();
      for (const t of tracks) {
        if (t.FileHash) {
          state.hashes.add(t.FileHash);
          trackArchive.set(t.FileHash, t);
          serverHashes.add(t.FileHash);
        }
      }
      reapplyIntent(serverHashes);
      state.loaded = true;
    } catch (e) {
      if (epoch === accountEpoch) {
        state.lastError = e instanceof Error ? e.message : String(e);
      }
    } finally {
      // Only release the flag if this reconcile still belongs to the current
      // account epoch; a newer epoch (account switch) owns the flag now.
      if (epoch === accountEpoch) {
        state.reconciling = false;
        reconcileInFlight = null;
      }
    }
  })();
  reconcileInFlight = promise;
  return reconcileInFlight;
}

function outboxCount(): number {
  return boundUserId ? loadOutbox(boundUserId).length : 0;
}

function refreshOutboxCount(): void {
  state.pendingOutbox = outboxCount();
}

/** Returns true if the op was persisted to the outbox, false on quota/private-mode failure. */
function enqueueOutboxOp(op: FavoriteOp): boolean {
  if (!boundUserId) return false;
  const ops = loadOutbox(boundUserId);
  ops.push(op);
  const ok = saveOutbox(boundUserId, ops);
  refreshOutboxCount();
  return ok;
}

function dequeueOutboxOp(hash: string, opId: number): void {
  if (!boundUserId) return;
  const ops = loadOutbox(boundUserId).filter((o) => !(o.fileHash === hash && o.opId === opId));
  saveOutbox(boundUserId, ops);
  refreshOutboxCount();
}

/**
 * Remove only the snapshot ops for `hash` from the CURRENT outbox, preserving
 * any new ops added during the replay wait (different opIds) and other hashes.
 * `snapshotOpIds` is the set of opIds captured when this flush started.
 */
function removeSnapshotOpsForHash(userId: string, hash: string, snapshotOpIds: Set<number>): void {
  const current = loadOutbox(userId);
  const filtered = current.filter(
    (o) => !(o.fileHash === hash && snapshotOpIds.has(o.opId)),
  );
  saveOutbox(userId, filtered);
  refreshOutboxCount();
}

/**
 * Replay pending offline operations (compacted to the latest op per hash = net
 * intent at snapshot time). Transport failures keep the op for the next replay;
 * business responses (success or status !== 1) drop the op since the server has
 * authoritative state. New ops added during the replay wait are preserved.
 */
async function flushOutbox(): Promise<void> {
  if (!boundUserId) return;
  const epoch = accountEpoch;
  const userId = boundUserId;
  const snapshot = loadOutbox(userId);
  if (snapshot.length === 0) {
    refreshOutboxCount();
    return;
  }
  const snapshotOpIds = new Set(snapshot.map((o) => o.opId));
  let liked: LikedPlaylistInfo | null;
  try {
    liked = await resolveLikedPlaylist();
  } catch {
    // Network unavailable - can't resolve the liked listid; keep ops for retry.
    return;
  }
  if (epoch !== accountEpoch || !liked) return; // switched, or can't replay without the liked listid
  const playlist: UserPlaylist = { id: liked.gid, listid: liked.listid, name: liked.name };

  // Latest snapshot op per hash = net intent at snapshot time.
  const latestByHash = new Map<string, FavoriteOp>();
  for (const op of snapshot) latestByHash.set(op.fileHash, op);

  for (const op of latestByHash.values()) {
    if (epoch !== accountEpoch) return; // account switched mid-replay
    let businessFailure = false;
    try {
      const res = op.favorite
        ? await addTrackToPlaylist(playlist, op.track)
        : await removeTrackFromPlaylist(playlist, op.track);
      businessFailure = !res.success;
    } catch {
      // Transport error: keep the snapshot ops for this hash for the next replay.
      continue;
    }
    if (epoch !== accountEpoch) return; // account switched while awaiting the adapter
    // Server responded (success or business failure): drop the snapshot ops for
    // this hash, preserving any new ops added during the wait.
    removeSnapshotOpsForHash(userId, op.fileHash, snapshotOpIds);
    if (businessFailure) {
      void reconcile();
    }
  }
}

async function sync(): Promise<void> {
  // Flush pending offline ops FIRST so the server reflects them before we
  // re-fetch ground truth.
  await flushOutbox();
  await reconcile();
}

async function setFavorite(track: Track, favorite: boolean): Promise<SetFavoriteResult> {
  const hash = track?.FileHash;
  if (!hash) return { status: 'failed', favorite, error: '无效曲目' };

  const opId = ++opCounter;
  lastOpId.set(hash, opId);
  trackArchive.set(hash, track);
  applyFavorite(hash, favorite); // optimistic, before any await

  // Not logged in: persist anonymously so it survives a reload and can be
  // migrated to the liked playlist on login.
  if (!boundUserId) {
    if (favorite) saveAnonymousFavorite(track);
    else removeAnonymousFavorite(hash);
    return { status: 'anonymous', favorite };
  }

  // Logged in: record the optimistic intent so a concurrent reconcile can't
  // clobber it before the server snapshot catches up. Cleared by reconcile once
  // the server agrees, or here on a terminal failure.
  pendingIntent.set(hash, favorite);

  const epoch = accountEpoch;
  let liked: LikedPlaylistInfo | null;
  try {
    liked = await resolveLikedPlaylist();
  } catch {
    // Network error resolving the liked playlist (e.g. first login while
    // offline, no cached id): queue for replay rather than rolling back, so the
    // favorite survives and is applied once the liked id resolves.
    if (epoch === accountEpoch && lastOpId.get(hash) === opId) {
      enqueueOutboxOp({ opId, fileHash: hash, favorite, track, ts: Date.now() });
    }
    return { status: 'pending', favorite };
  }
  if (epoch !== accountEpoch) return { status: 'pending', favorite }; // account switched
  if (!liked) {
    // Genuinely no liked playlist to target: roll back the optimistic change.
    if (lastOpId.get(hash) === opId) {
      applyFavorite(hash, !favorite);
      pendingIntent.delete(hash);
    }
    return { status: 'failed', favorite, error: '未找到「我喜欢的音乐」歌单' };
  }

  const playlist: UserPlaylist = { id: liked.gid, listid: liked.listid, name: liked.name };
  try {
    const res = favorite
      ? await addTrackToPlaylist(playlist, track)
      : await removeTrackFromPlaylist(playlist, track);

    if (epoch !== accountEpoch) return { status: 'pending', favorite }; // account switched

    // Only the LATEST op for this hash may confirm state.
    if (lastOpId.get(hash) !== opId) {
      // Stale: a newer op supersedes this. Never relight from an old response.
      dequeueOutboxOp(hash, opId);
      return { status: 'pending', favorite };
    }
    dequeueOutboxOp(hash, opId);
    if (!res.success) {
      // Business failure on the latest op: roll back and re-sync from server.
      applyFavorite(hash, !favorite);
      pendingIntent.delete(hash);
      void reconcile();
      return { status: 'failed', favorite, error: res.error || '收藏失败' };
    }
    // Confirmed: keep pendingIntent so a stale in-flight reconcile can't
    // unlight this; the next reconcile clears it once the server agrees.
    return { status: 'confirmed', favorite };
  } catch {
    // Transport error (offline / circuit open): persist for replay, but only if
    // this is still the latest op for the hash AND the account hasn't switched.
    if (epoch === accountEpoch && lastOpId.get(hash) === opId) {
      enqueueOutboxOp({ opId, fileHash: hash, favorite, track, ts: Date.now() });
      return { status: 'pending', favorite };
    }
    return { status: 'pending', favorite };
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
  // Only bump the epoch (invalidating in-flight sync) on an ACTUAL account
  // switch. Same-account re-login must not bump: otherwise the in-flight
  // reconcile's finally (epoch mismatch) never releases reconcileInFlight, and
  // every subsequent reconcile returns the stale promise forever.
  const switching = !!boundUserId && boundUserId !== userId;
  if (switching) {
    accountEpoch += 1;
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

  // Migrate anonymous favorites (created while logged out) into this user's
  // outbox so sync replays them to the liked playlist. Persist the outbox
  // FIRST, then clear the anonymous source - and only if every op actually
  // persisted, so a quota/private-mode write failure cannot lose the favorites.
  const anon = loadAnonymousFavorites();
  let allPersisted = true;
  for (const track of anon) {
    if (!track.FileHash) continue;
    const opId = ++opCounter;
    lastOpId.set(track.FileHash, opId);
    trackArchive.set(track.FileHash, track);
    applyFavorite(track.FileHash, true); // preliminary until reconcile confirms
    pendingIntent.set(track.FileHash, true);
    const ok = enqueueOutboxOp({ opId, fileHash: track.FileHash, favorite: true, track, ts: Date.now() });
    if (!ok) allPersisted = false;
  }
  if (anon.length && allPersisted) clearAnonymousFavorites();

  refreshOutboxCount();
  await sync();
}

function onLogout(): void {
  accountEpoch += 1;
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
  setFavorite(track: Track, favorite: boolean): Promise<SetFavoriteResult> {
    return setFavorite(track, favorite);
  },
  markFavoriteByHash(hash: string, favorite: boolean): void {
    markFavoriteByHash(hash, favorite);
  },
  /**
   * Mark a single track favorite AND archive it (no API call). Used after an
   * external caller (e.g. AddToPlaylistModal) has already performed the add via
   * the adapter, so the heart lights up and a later unfavorite has the fileid.
   */
  markFavoriteTrack(track: Track): void {
    if (!track?.FileHash) return;
    state.hashes.add(track.FileHash);
    trackArchive.set(track.FileHash, track);
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
  accountEpoch += 1; // invalidate any in-flight async from a prior test
  resetInMemory();
}

/** Reactive readonly view of the favorite state (for compat projections). */
export const favoriteStateView = readonly(state);

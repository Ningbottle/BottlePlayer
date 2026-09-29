import { apiGet } from "../../platform/tauri/nativeClient";

export interface UserPlaylistsResponse {
  status: number;
  error_code?: number | string;
  error?: string;
  data?: {
    info?: unknown[];
    list?: unknown[];
    skipped_invalid_id_count?: number;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

export interface PlaylistTracksResponse<T = unknown> {
  status: number;
  error_code?: number | string;
  error?: string;
  data?: {
    list: T[];
    total: number;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

export async function fetchUserPlaylistsRaw(page = 1, pagesize = 100): Promise<UserPlaylistsResponse> {
  return apiGet<UserPlaylistsResponse>("/user/playlist", { page, pagesize });
}

export async function fetchPlaylistTracks<T = unknown>(params: {
  id: string;
  page?: number;
  pagesize?: number;
}): Promise<PlaylistTracksResponse<T>> {
  return apiGet<PlaylistTracksResponse<T>>("/playlist/track/all", params);
}

/** Result of walking every page of a playlist. */
export interface FullPlaylistResult<T> {
  tracks: T[];
  /** total reported by the backend (0 when the first page never answered). */
  total: number;
  pagesFetched: number;
  /**
   * True when the walk did not end with the complete playlist: a page failed,
   * the page cap was hit, or the caller cancelled. Callers must surface this —
   * a truncated queue must never be presented as "the whole playlist".
   */
  incomplete: boolean;
  /** First page/transport error, when one happened. */
  error?: string;
  /** True when isCurrent() reported stale before the walk finished. */
  cancelled: boolean;
}

const FULL_PLAYLIST_DEFAULT_PAGE_SIZE = 50;
/** Hard cap so a bogus/huge `total` cannot spin thousands of requests. */
const FULL_PLAYLIST_MAX_PAGES = 40;

/**
 * Fetch every track of a playlist by walking its pages.
 *
 * `isCurrent` is consulted before each request and before returning: a playlist
 * view that was closed, a page change, or an account switch must stop the walk
 * and must not have its result applied. Items are de-duplicated by stable
 * identity because upstream pages can overlap, and a mid-walk failure keeps the
 * partial result with `incomplete: true` rather than pretending it is whole.
 */
export async function fetchAllPlaylistTracks<T extends Record<string, any>>(params: {
  id: string;
  pageSize?: number;
  maxPages?: number;
  isCurrent?: () => boolean;
  identityOf?: (item: T) => string | null;
}): Promise<FullPlaylistResult<T>> {
  const pageSize = Math.max(1, Math.floor(params.pageSize ?? FULL_PLAYLIST_DEFAULT_PAGE_SIZE));
  const maxPages = Math.max(1, Math.floor(params.maxPages ?? FULL_PLAYLIST_MAX_PAGES));
  const isCurrent = params.isCurrent ?? (() => true);
  const identityOf = params.identityOf ?? ((item: T) => {
    const mid = item?.SongMid ?? item?.songmid ?? item?.mid;
    if (typeof mid === 'string' && mid.trim()) return `mid:${mid.trim()}`;
    const id = item?.id ?? item?.trackId;
    if (id !== undefined && id !== null && String(id).trim()) return `id:${String(id).trim()}`;
    const hash = item?.FileHash ?? item?.filehash;
    if (typeof hash === 'string' && hash.trim()) return `hash:${hash.trim().toLowerCase()}`;
    return null;
  });

  const tracks: T[] = [];
  const seen = new Set<string>();
  const seenPages = new Set<string>();
  let total = 0;
  let pagesFetched = 0;
  let error: string | undefined;

  for (let page = 1; page <= maxPages; page += 1) {
    if (!isCurrent()) {
      return { tracks, total, pagesFetched, incomplete: true, cancelled: true, error };
    }
    let res: PlaylistTracksResponse<T>;
    try {
      res = await fetchPlaylistTracks<T>({ id: params.id, page, pagesize: pageSize });
    } catch (err: any) {
      error = typeof err?.message === 'string' ? err.message : String(err);
      return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
    }
    if (!isCurrent()) {
      return { tracks, total, pagesFetched, incomplete: true, cancelled: true, error };
    }
    if (res.status !== 1 || !res.data) {
      error = res.error || `第 ${page} 页曲目获取失败`;
      return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
    }
    pagesFetched += 1;
    const list = Array.isArray(res.data.list) ? res.data.list : [];
    total = typeof res.data.total === 'number' && Number.isFinite(res.data.total) && res.data.total >= 0
      ? res.data.total
      : total;
    const pageIdentities: string[] = [];
    let allItemsHaveIdentity = list.length > 0;
    let addedTracks = 0;
    for (const item of list) {
      if (!item || typeof item !== 'object') continue;
      const identity = identityOf(item);
      if (identity !== null) {
        pageIdentities.push(identity);
        if (seen.has(identity)) continue;
        seen.add(identity);
      } else {
        allItemsHaveIdentity = false;
      }
      tracks.push(item);
      addedTracks += 1;
    }

    // A repeated full page means pagination is returning stale data. Stop the
    // walk rather than burning through the page cap and claiming completion.
    const pageFingerprint = allItemsHaveIdentity && pageIdentities.length === list.length
      ? JSON.stringify(pageIdentities)
      : null;
    if (pageFingerprint && seenPages.has(pageFingerprint)) {
      error = `第 ${page} 页与之前页面重复，无法确认歌单完整`;
      return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
    }
    if (pageFingerprint) seenPages.add(pageFingerprint);

    if (total > 0 && tracks.length >= total) return { tracks, total, pagesFetched, incomplete: false, cancelled: false };

    // A short page is normally the end, but if the backend reported more
    // tracks than we could collect, surface the contradiction as partial data.
    if (list.length < pageSize) {
      if (total > tracks.length) {
        error = `分页提前结束：已获取 ${tracks.length}/${total} 首`;
        return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
      }
      return { tracks, total: total || tracks.length, pagesFetched, incomplete: false, cancelled: false };
    }

    // A full page with no new stable identities is another stale-page signal,
    // including when the server omits total. Keep the rows already collected
    // and let the caller report the queue as incomplete.
    if (list.length > 0 && addedTracks === 0) {
      error = `第 ${page} 页没有新曲目，无法确认歌单完整`;
      return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
    }
  }

  // Reaching the cap is not proof of completion when the last page was full,
  // especially when the backend omits total. Callers must surface truncation.
  error = error || `已达到 ${maxPages} 页上限，无法确认歌单完整`;
  return { tracks, total, pagesFetched, incomplete: true, cancelled: false, error };
}

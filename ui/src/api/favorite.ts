import { apiGet, apiPost } from './backend';
import { userStore } from './userStore';
import { Track } from './normalizer';

// ── 歌单解析工具（供 Sidebar / AddToPlaylistModal 共用） ──

export interface UserPlaylist {
  id: string;
  name: string;
  songcount?: number;
  /** 酷狗 listid（纯数字，用于 AddPlaylistTracks API） */
  listid?: string;
}

type RawRecord = Record<string, unknown>;

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
      // 优先使用 listid（纯数字），用于 C++ 后端 AddPlaylistTracks
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

/** 收藏歌曲到指定歌单 */
export async function addTrackToPlaylist(
  playlist: UserPlaylist,
  track: Track
): Promise<{ success: boolean; error?: string }> {
  if (!userStore.isLoggedIn) {
    return { success: false, error: '请先登录' };
  }

  const apiId = playlist.listid || playlist.id;

  try {
    // 构造歌曲信息：name|hash|album_id|mixsongid
    // SongName 中可能含 | 字符，需要转义以避免后端解析错位
    const safeName = track.SongName.replace(/\|/g, '%7C');
    const trackInfo = `${safeName}|${track.FileHash}|${track.AlbumID || 0}|${track.AlbumAudioID || 0}`;
    
    const res = await apiPost<any>('/playlist/tracks/add', undefined, {
      listid: apiId,
      data: trackInfo  // 后端期望参数名 "data"
    });

    if (res?.status === 1) {
      return { success: true };
    }
    return { success: false, error: res?.error || '收藏失败' };
  } catch (e: any) {
    return { success: false, error: e.message || '收藏失败' };
  }
}

/** 获取用户歌单列表 */
export async function getUserPlaylists(): Promise<UserPlaylist[]> {
  if (!userStore.isLoggedIn) {
    return [];
  }

  try {
    const res = await apiGet<any>('/user/playlist', { page: 1, pagesize: 100 });
    return normalizePlaylists(res);
  } catch (e) {
    console.error('获取歌单列表失败', e);
    return [];
  }
}

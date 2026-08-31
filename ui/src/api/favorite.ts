import { addPlaylistTracks, removePlaylistTracks as removePlaylistTracksCall } from '../features/library/favoriteGateway';
import { userStore } from './userStore';
import type { Track } from '../shared/music/track';

/**
 * favorite.ts is ONLY the /playlist/tracks/add and /playlist/tracks/del adapter.
 * Playlist discovery (getUserPlaylists / normalizePlaylists) and favorite-state
 * authority live in favoriteStore.ts. Read the native contract before changing
 * parameter names - see native/core/compat_routes/PlaylistRoutes.cpp and
 * native/core/PlaylistService.cpp.
 */

export interface UserPlaylist {
  id: string;
  name: string;
  songcount?: number;
  /** 酷狗 listid（纯数字，用于 AddPlaylistTracks / DeletePlaylistTracks） */
  listid?: string;
}

/**
 * Build the `name|hash|album_id|mixsongid` record the native
 * AddPlaylistTracks handler expects (comma-separated list of these records).
 * SongName may contain `|`, so escape it to avoid field misalignment.
 */
function buildTrackInfo(track: Track): string {
  const safeName = (track.SongName || '').replace(/\|/g, '%7C');
  return `${safeName}|${track.FileHash || ''}|${track.AlbumID || 0}|${track.AlbumAudioID || 0}`;
}

/**
 * 收藏歌曲到指定歌单 (POST /playlist/tracks/add).
 *
 * Contract (HandlePlaylistTracksAdd → AddPlaylistTracks): the route reads
 * `listid` and `data` from the query (or JSON body). `data` is a
 * comma-separated list of `name|hash|album_id|mixsongid` records. AddPlaylistTracks
 * extracts the numeric listid from a `collection_…` gid, so either form works.
 *
 * Transport failures (network / circuit_open / non-2xx) THROW so callers can
 * route them to the outbox; business failures (status !== 1) return
 * `{ success: false, error }`.
 */
export async function addTrackToPlaylist(
  playlist: UserPlaylist,
  track: Track,
): Promise<{ success: boolean; error?: string }> {
  if (!userStore.isLoggedIn) return { success: false, error: '请先登录' };

  const apiId = playlist.listid || playlist.id;
  const res = await addPlaylistTracks({
    listid: apiId,
    data: buildTrackInfo(track),
  });

  if (res?.status === 1) return { success: true };
  return { success: false, error: res?.error || '收藏失败' };
}

/**
 * 从指定歌单移除歌曲 (POST /playlist/tracks/del).
 *
 * Contract (HandlePlaylistTracksDel → DeletePlaylistTracks): the route reads
 * `listid` and `fileids` from the query. `fileids` is a comma-separated list of
 * NUMERIC fileids (= the track's `audio_id`, NOT its FileHash). Unlike add,
 * DeletePlaylistTracks does NOT extract a numeric id from a `collection_…` gid,
 * so the numeric `listid` must be passed.
 *
 * Transport failures THROW; business failures return `{ success: false, error }`.
 */
export async function removeTrackFromPlaylist(
  playlist: UserPlaylist,
  track: Track,
): Promise<{ success: boolean; error?: string }> {
  if (!userStore.isLoggedIn) return { success: false, error: '请先登录' };

  const apiId = playlist.listid || playlist.id;
  const fileid = String(track.fileid ?? track.audio_id ?? '');
  if (!fileid) return { success: false, error: '缺少曲目 fileid，无法取消收藏' };

  const res = await removePlaylistTracksCall({
    listid: apiId,
    fileids: fileid,
  });

  if (res?.status === 1) return { success: true };
  return { success: false, error: res?.error || '取消收藏失败' };
}

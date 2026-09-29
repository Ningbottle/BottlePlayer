import { apiPost } from "../../platform/tauri/nativeClient";

export interface ModifyPlaylistTracksResponse {
  status: number;
  error?: string;
  [key: string]: unknown;
}

export interface PlaylistTrackInput {
  name: string;
  hash: string;
  album_id: string | number;
  mixsongid: string | number;
}

export async function addPlaylistTracks(params: {
  listid: string;
  data: PlaylistTrackInput[];
}): Promise<ModifyPlaylistTracksResponse> {
  return apiPost<ModifyPlaylistTracksResponse>("/playlist/tracks/add", JSON.stringify(params));
}

export async function removePlaylistTracks(params: {
  listid: string;
  fileids: string;
}): Promise<ModifyPlaylistTracksResponse> {
  return apiPost<ModifyPlaylistTracksResponse>("/playlist/tracks/del", undefined, params);
}

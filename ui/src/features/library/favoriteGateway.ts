import { apiPost } from "../../platform/tauri/nativeClient";

export interface ModifyPlaylistTracksResponse {
  status: number;
  error?: string;
  [key: string]: unknown;
}

export async function addPlaylistTracks(params: {
  listid: string;
  data: string;
}): Promise<ModifyPlaylistTracksResponse> {
  return apiPost<ModifyPlaylistTracksResponse>("/playlist/tracks/add", undefined, params);
}

export async function removePlaylistTracks(params: {
  listid: string;
  fileids: string;
}): Promise<ModifyPlaylistTracksResponse> {
  return apiPost<ModifyPlaylistTracksResponse>("/playlist/tracks/del", undefined, params);
}

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

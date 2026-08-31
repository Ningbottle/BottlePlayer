import { apiGet } from "../../platform/tauri/nativeClient";

export interface FeedResponse {
  status?: number;
  data?: unknown;
  error?: string;
  [key: string]: unknown;
}

export async function fetchEverydayRecommend(pagesize = 6): Promise<FeedResponse> {
  return apiGet<FeedResponse>("/everyday/recommend", { pagesize });
}

export async function fetchTopSong(pagesize = 6): Promise<FeedResponse> {
  return apiGet<FeedResponse>("/top/song", { pagesize });
}

export async function fetchTopPlaylist(params: { pagesize: number; sort: number }): Promise<FeedResponse> {
  return apiGet<FeedResponse>("/top/playlist", params);
}

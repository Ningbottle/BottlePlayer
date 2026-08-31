import { apiGet } from "../../platform/tauri/nativeClient";

export interface SearchSongsResponse<T = unknown> {
  status: number;
  error?: string;
  data?: {
    lists: T[];
    total: number;
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

export async function searchSongs<T = unknown>(params: {
  keywords: string;
  page?: number;
  pagesize?: number;
}): Promise<SearchSongsResponse<T>> {
  return apiGet<SearchSongsResponse<T>>("/search", params);
}

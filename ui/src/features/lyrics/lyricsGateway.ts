import { apiGet } from "../../platform/tauri/nativeClient";

export interface LyricCandidate {
  id: string;
  accesskey: string;
  [key: string]: unknown;
}

export interface LyricSearchResponse {
  status: number;
  candidates?: LyricCandidate[];
  error?: string;
  [key: string]: unknown;
}

export interface LyricDetailResponse {
  status: number;
  lyric?: string;
  error?: string;
  [key: string]: unknown;
}

export async function searchLyricCandidates(hash: string): Promise<LyricSearchResponse> {
  return apiGet<LyricSearchResponse>("/search/lyric", { hash });
}

export async function fetchLyricDetail(id: string, accesskey: string): Promise<LyricDetailResponse> {
  return apiGet<LyricDetailResponse>("/lyric", { id, accesskey });
}

import { apiGet } from "../../platform/tauri/nativeClient";

export interface PersonalFmParams {
  hash?: string;
  songid?: string | number;
  playtime?: number;
  remain_songcnt?: number;
  is_overplay?: number;
  [key: string]: string | number | undefined;
}

export interface PersonalFmResponse {
  status?: number;
  error?: string;
  data?: unknown;
  [key: string]: unknown;
}

export async function fetchPersonalFm(params: PersonalFmParams): Promise<PersonalFmResponse> {
  const query: Record<string, string | number> = {};
  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined) {
      query[key] = value;
    }
  }
  return apiGet<PersonalFmResponse>("/personal/fm", query);
}

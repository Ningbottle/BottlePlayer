import { apiGet } from "../../platform/tauri/nativeClient";

export interface UserHistoryResponse {
  status: number;
  error?: string;
  data?: {
    info?: unknown[];
    list?: unknown[];
    songs?: unknown[];
    data?: unknown[];
    [key: string]: unknown;
  };
  [key: string]: unknown;
}

export async function fetchUserHistory(pagesize = 100): Promise<UserHistoryResponse> {
  return apiGet<UserHistoryResponse>("/user/history", { pagesize });
}

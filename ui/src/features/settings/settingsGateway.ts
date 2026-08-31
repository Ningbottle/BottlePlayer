import { apiGet, apiPost } from "../../platform/tauri/nativeClient";

export interface MemoryData {
  working_set_bytes: number;
  private_bytes: number;
  image_cache_bytes: number;
  pending_task_count: number;
  playback_state: string;
  text: string;
}

export interface DiagnosticsMemoryResponse {
  status: number;
  data?: MemoryData;
  error?: string;
  [key: string]: unknown;
}

export interface DeviceInfo {
  dfid: string;
  mid: string;
  uuid: string;
  appid: string;
  clientver: string;
  registered: boolean;
}

export interface DeviceSettingsResponse {
  status: number;
  data: DeviceInfo;
  error?: string;
  [key: string]: unknown;
}

export interface SaveDeviceSettingsResponse {
  status: number;
  data: DeviceInfo;
  updated: boolean;
  error?: string;
  [key: string]: unknown;
}

export interface ProbeSongUrlResponse {
  status: number;
  url?: string;
  error?: string;
  [key: string]: unknown;
}

export interface YouthVipResponse {
  status: number;
  error_code?: number | string;
  error_msg?: string;
  data?: unknown;
  [key: string]: unknown;
}

export async function fetchDiagnosticsMemory(): Promise<DiagnosticsMemoryResponse> {
  return apiGet<DiagnosticsMemoryResponse>("/diagnostics/memory");
}

export async function fetchDeviceSettings(): Promise<DeviceSettingsResponse> {
  return apiGet<DeviceSettingsResponse>("/settings/device");
}

export async function saveDeviceSettings(query: Record<string, string>): Promise<SaveDeviceSettingsResponse> {
  return apiPost<SaveDeviceSettingsResponse>("/settings/device", undefined, query);
}

export async function resetDeviceSettings(): Promise<DeviceSettingsResponse> {
  return apiPost<DeviceSettingsResponse>("/settings/device", undefined, { clear: "1" });
}

export async function probeSongUrl(params: {
  hash: string;
  album_id?: string;
  album_audio_id?: string;
}): Promise<ProbeSongUrlResponse> {
  return apiGet<ProbeSongUrlResponse>("/song/url", params);
}

export async function claimYouthListenSong(): Promise<YouthVipResponse> {
  return apiGet<YouthVipResponse>("/youth/listen/song");
}

export async function claimYouthVipAd(): Promise<YouthVipResponse> {
  return apiGet<YouthVipResponse>("/youth/vip/ad");
}

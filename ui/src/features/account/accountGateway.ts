import { apiGet, apiPost } from "../../platform/tauri/nativeClient";

export interface DeviceRegisterData {
  registered?: boolean;
  dfid?: string;
  [key: string]: unknown;
}

export interface DeviceRegisterResponse {
  status: number;
  data?: DeviceRegisterData;
  error?: string;
}

export interface UserDetailData {
  userid?: string | number;
  nickname?: string;
  username?: string;
  pic?: string;
  avatar?: string;
  [key: string]: unknown;
}

export interface UserDetailResponse {
  status: number;
  data?: UserDetailData;
  error?: string;
}

export interface QrKeyData {
  qrcode?: string;
  qrcode_img?: string;
  imgurl?: string;
  img_url?: string;
  img?: string;
  qrcodeurl?: string;
  [key: string]: unknown;
}

export interface QrKeyResponse {
  status: number;
  data?: QrKeyData;
  error?: string;
}

export interface QrCheckData {
  status?: number;
  [key: string]: unknown;
}

export interface QrCheckResponse {
  status: number;
  data?: QrCheckData;
  error?: string;
}

export interface YouthListenSongResponse {
  status: number;
  error_code?: number | string;
  error_msg?: string;
  data?: unknown;
}

export interface YouthVipAdResponse {
  status: number;
  error_code?: number | string;
  error_msg?: string;
  data?: unknown;
}

export interface LogoutResponse {
  status: number;
  error?: string;
}

export async function registerDevice(): Promise<DeviceRegisterResponse> {
  return apiPost<DeviceRegisterResponse>("/register/dev");
}

export async function fetchUserDetail(): Promise<UserDetailResponse> {
  return apiGet<UserDetailResponse>("/user/detail");
}

export async function fetchVipDetail(): Promise<unknown> {
  return apiGet<unknown>("/user/vip/detail");
}

export async function claimDailyVipSong(): Promise<YouthListenSongResponse> {
  return apiGet<YouthListenSongResponse>("/youth/listen/song");
}

export async function claimYouthListenSong(): Promise<YouthListenSongResponse> {
  return apiGet<YouthListenSongResponse>("/youth/listen/song");
}

export async function claimYouthVipAd(): Promise<YouthVipAdResponse> {
  return apiGet<YouthVipAdResponse>("/youth/vip/ad");
}

export async function fetchQrKey(): Promise<QrKeyResponse> {
  return apiGet<QrKeyResponse>("/login/qr/key");
}

export async function checkQrStatus(key: string): Promise<QrCheckResponse> {
  return apiGet<QrCheckResponse>("/login/qr/check", { key });
}

export async function logoutAuth(): Promise<LogoutResponse> {
  return apiPost<LogoutResponse>("/auth/logout");
}

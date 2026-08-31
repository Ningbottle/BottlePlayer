import { apiGet } from '../../platform/tauri/nativeClient';
import type { Track } from '../../shared/music/track';
import type { ResolveTrackResult } from '../types';

export interface ProbeSongUrlParams {
  hash: string;
  album_id?: string;
  album_audio_id?: string;
  quality?: string;
}

export interface ProbeSongUrlResponse {
  status: number;
  url?: string;
  error?: string;
  [key: string]: unknown;
}

export function resolveTrack(
  track: Track,
  quality: string,
): Promise<ResolveTrackResult> {
  return apiGet<ResolveTrackResult>('/song/url', {
    hash: track.FileHash,
    album_id: track.AlbumID || '',
    album_audio_id: track.AlbumAudioID || '',
    quality,
  });
}

export function probeSongUrl(params: ProbeSongUrlParams): Promise<ProbeSongUrlResponse> {
  return apiGet<ProbeSongUrlResponse>('/song/url', {
    hash: params.hash,
    ...(params.album_id !== undefined ? { album_id: params.album_id } : {}),
    ...(params.album_audio_id !== undefined ? { album_audio_id: params.album_audio_id } : {}),
    ...(params.quality !== undefined ? { quality: params.quality } : {}),
  });
}

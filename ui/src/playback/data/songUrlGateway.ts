import { apiGet } from '../../platform/tauri/nativeClient';
import type { Track } from '../../shared/music/track';
import type { Delivery, ResolveTrackResult } from '../types';
import type { QualityOption } from '../runtime/playbackOrchestrator';

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

export async function resolveTrack(
  track: Track,
  quality: string,
): Promise<ResolveTrackResult> {
  const result = await apiGet<ResolveTrackResult>('/song/url', {
    hash: track.FileHash,
    album_id: track.AlbumID || '',
    album_audio_id: track.AlbumAudioID || '',
    quality,
  });
  const normalizeDelivery = (value: unknown, preview: boolean): Delivery =>
    value === 'full' || value === 'preview' || value === 'unknown'
      ? value : preview ? 'preview' : 'unknown';
  result.delivery = normalizeDelivery(result.delivery, !!result.is_preview);
  result.is_preview = result.delivery === 'preview';
  if (result.data?.available_qualities) {
    result.data.available_qualities = result.data.available_qualities.map((entry) => {
      const raw = entry as QualityOption & { is_preview?: boolean };
      const delivery = normalizeDelivery(raw.delivery, raw.is_preview ?? !!raw.isPreview);
      return { ...entry, delivery, isPreview: delivery === 'preview' };
    });
  }
  return result;
}

export function probeSongUrl(params: ProbeSongUrlParams): Promise<ProbeSongUrlResponse> {
  return apiGet<ProbeSongUrlResponse>('/song/url', {
    hash: params.hash,
    ...(params.album_id !== undefined ? { album_id: params.album_id } : {}),
    ...(params.album_audio_id !== undefined ? { album_audio_id: params.album_audio_id } : {}),
    ...(params.quality !== undefined ? { quality: params.quality } : {}),
  });
}

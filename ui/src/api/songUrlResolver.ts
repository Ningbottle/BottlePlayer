import { apiGet } from '../platform/tauri/nativeClient';
import type { Track } from './normalizer';
import type { ResolveTrackResult } from '../playback/types';

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

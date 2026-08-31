import { apiGet } from '../../platform/tauri/nativeClient';

export interface CoverImageItem {
  sizable_portrait?: string;
  sizable_avatar?: string;
  imgs?: Record<string, Array<{ sizable_portrait?: string; sizable_avatar?: string }>>;
}

export interface CoverImageResponse {
  status: number;
  data?: CoverImageItem | CoverImageItem[] | CoverImageItem[][];
}

/**
 * Cover I/O gateway — the only /images/audio caller. Best-effort: any failure
 * resolves to '' so queue rendering never breaks on a missing cover.
 */
export async function fetchCoverImage(hash: string, albumAudioId: string = ''): Promise<string> {
  if (!hash) return '';
  try {
    const res = await apiGet<CoverImageResponse>('/images/audio', {
      hash: hash,
      album_audio_id: albumAudioId,
    });
    if (res && res.status === 1 && res.data) {
      let item: CoverImageItem | null = null;
      if (Array.isArray(res.data) && res.data[0]) {
        const first = res.data[0];
        item = Array.isArray(first) ? first[0] ?? null : first;
      } else if (!Array.isArray(res.data)) {
        item = res.data;
      }

      if (item) {
        if (item.sizable_portrait) return item.sizable_portrait.replace('{size}', '400');
        if (item.sizable_avatar) return item.sizable_avatar.replace('{size}', '400');
        if (item.imgs) {
          const keys = Object.keys(item.imgs).sort((a, b) => Number(b) - Number(a));
          if (keys.length > 0) {
            const imgArr = item.imgs[keys[0]];
            if (imgArr && imgArr.length > 0 && imgArr[0].sizable_portrait) {
              return imgArr[0].sizable_portrait.replace('{size}', '400');
            }
          }
        }
      }
    }
  } catch (e) {
    console.error('Failed to fetch cover image for hash', hash, e);
  }
  return '';
}

import { apiPost } from '../../platform/tauri/nativeClient';
import type { Track } from '../../api/normalizer';
import { userStore } from '../../api/userStore';

/** Upload play history to KuGou (silent failure). */
export async function uploadPlayHistory(track: Track): Promise<void> {
  try {
    if (!userStore.isLoggedIn) return;
    const mxid = track.AlbumAudioID || track.MixSongID;
    if (!mxid) return;
    const numMxid = Number(mxid);
    if (!Number.isFinite(numMxid) || numMxid <= 0) return;
    await apiPost('/playhistory/upload', undefined, {
      mxid: numMxid,
      time: Math.floor(Date.now() / 1000),
      pc: 1,
    });
  } catch (e) {
    console.warn('播放历史上传失败（可忽略）:', e);
  }
}

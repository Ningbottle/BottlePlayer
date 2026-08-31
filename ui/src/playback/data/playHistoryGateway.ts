import { apiPost } from '../../platform/tauri/nativeClient';
import type { Track } from '../../shared/music/track';

/**
 * Whether the current account may upload play history. The composition root
 * binds this to the account store's login state; Playback depends on the
 * injected policy only — never on the account module or its types.
 */
export interface PlayHistoryPolicy {
  isUploadEnabled(): boolean;
}

/** Default: uploads disabled until the composition root configures a policy. */
const defaultPolicy: PlayHistoryPolicy = {
  isUploadEnabled: () => false,
};

let policy: PlayHistoryPolicy | null = null;

export function configurePlayHistoryPolicy(configured: PlayHistoryPolicy): void {
  policy = configured;
}

/** Test-only: restore the default-disabled policy between tests. */
export function __resetPlayHistoryPolicyForTests(): void {
  policy = null;
}

/** Upload play history to KuGou (silent failure). */
export async function uploadPlayHistory(track: Track): Promise<void> {
  try {
    if (!(policy ?? defaultPolicy).isUploadEnabled()) return;
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

/**
 * Play stats gateway — the only module in UI allowed to issue the
 * `stats_record_play` command. Fire-and-forget: stats are non-critical, so both a
 * synchronous throw and a rejected Promise are swallowed; playback must never
 * be affected by stats IPC failure.
 */
import { invokeTauri } from '../../platform/tauri/invoke';
import type { PlayRecord } from '../playSessionTracker';

export function recordPlay(record: PlayRecord): void {
  try {
    invokeTauri('stats_record_play', { json: JSON.stringify(record) }).catch(() => {});
  } catch {
    // 静默失败：统计记录不影响播放
  }
}

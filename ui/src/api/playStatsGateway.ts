/**
 * Play stats gateway — the only module in UI allowed to invoke
 * `stats_record_play`. Fire-and-forget: stats are non-critical, so both a
 * synchronous throw and a rejected Promise are swallowed; playback must never
 * be affected by stats IPC failure.
 */
import { invoke } from '@tauri-apps/api/core';
import type { PlayRecord } from '../playback/playSessionTracker';

export function recordPlay(record: PlayRecord): void {
  try {
    invoke('stats_record_play', { json: JSON.stringify(record) }).catch(() => {});
  } catch {
    // 静默失败：统计记录不影响播放
  }
}

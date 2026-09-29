/**
 * Play stats gateway — the only module in UI allowed to issue the
 * `stats_record_play` command. Fire-and-forget: stats are non-critical, so both a
 * synchronous throw and a rejected Promise are logged without propagating;
 * playback must never be affected by stats IPC failure. Do not retry a record
 * automatically: the previous attempt may have committed before timing out.
 */
import { invokeTauri } from '../../platform/tauri/invoke';
import type { PlayRecord } from '../playSessionTracker';

export function recordPlay(record: PlayRecord): void {
  try {
    invokeTauri('stats_record_play', { json: JSON.stringify(record) }).catch(reportFailure);
  } catch (error) {
    reportFailure(error);
  }
}

function reportFailure(error: unknown): void {
  console.warn('播放统计记录失败（不影响播放）:', error);
}

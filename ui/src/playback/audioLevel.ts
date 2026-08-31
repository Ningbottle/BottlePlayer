/**
 * UI-safe audio-level adapter.
 *
 * The Feature layer must not know that Playback owns an <audio> element or
 * where it lives (MediaRuntime). This adapter reads the runtime audio inside
 * the playback slice and hands the caller an opaque level monitor. Returns
 * null when no media runtime is bound yet (callers treat that as "inert").
 */
import { getMediaRuntime } from './runtime/mediaRuntime';
import {
  createAudioLevelMonitor,
  type AudioLevelMonitor,
} from './runtime/audioLevelMonitor';

export type { AudioLevelMonitor };

export function createPlaybackAudioLevelMonitor(): AudioLevelMonitor | null {
  const audio = getMediaRuntime()?.audio;
  if (!audio) return null;
  return createAudioLevelMonitor(audio);
}

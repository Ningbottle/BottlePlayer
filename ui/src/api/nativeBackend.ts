import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';

export class NativePlaybackBackend implements PlayerBackend {
  readonly kind = 'native' as const;
  private initialized = false;
  private backendUsed: 'mfs' | 'mfp' | null = null;

  async initialize(): Promise<boolean> {
    if (this.initialized) return true;
    // Try MFS first (IMFMediaSession + EQ)
    let ok = await invoke<boolean>('playback_initialize', { backend: 1 });
    if (ok) {
      this.backendUsed = 'mfs';
    } else {
      // Fallback: MFP (MFPlay, no EQ)
      ok = await invoke<boolean>('playback_initialize', { backend: 0 });
      if (ok) this.backendUsed = 'mfp';
    }
    if (!ok) return false;
    this.initialized = true;
    return true;
  }

  get activeBackendKind() { return this.backendUsed; }

  async playUrl(url: string): Promise<boolean> {
    return invoke<boolean>('playback_play_url', { url });
  }

  async pause(): Promise<void> { await invoke('playback_pause'); }
  async resume(): Promise<void> { await invoke('playback_resume'); }
  async stop(): Promise<void> { await invoke('playback_stop'); }
  async seek(seconds: number): Promise<void> {
    await invoke('playback_seek', { seconds });
  }
  async setVolume(v: number): Promise<void> {
    await invoke('playback_set_volume', { volume: v });
  }
  async setRate(r: number): Promise<void> {
    await invoke('playback_set_rate', { rate: r });
  }

  async getState(): Promise<PlaybackState> {
    const json = await invoke<string>('playback_get_state');
    return JSON.parse(json);
  }

  async shutdown(): Promise<void> { await invoke('playback_shutdown'); }

  onEvent(cb: (e: PlaybackEvent) => void): () => void {
    let unlisten: UnlistenFn | null = null;
    listen<string>('playback_event', (ev) => {
      try {
        const data = JSON.parse(ev.payload);
        cb(data);
      } catch (e) {
        console.warn('Failed to parse playback_event:', e);
      }
    }).then((u) => { unlisten = u; });
    return () => { unlisten?.(); };
  }
}

import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';

export class Html5AudioBackend implements PlayerBackend {
  readonly kind = 'html5' as const;

  constructor(private audio: HTMLAudioElement) {
    this.audio.volume = parseFloat(localStorage.getItem('player_volume') || '0.7');
  }

  async initialize(): Promise<boolean> { return true; }

  async playUrl(url: string): Promise<boolean> {
    this.audio.src = url;
    try {
      await this.audio.play();
      return true;
    } catch (e) {
      console.warn('Html5AudioBackend play failed:', e);
      return false;
    }
  }

  async pause(): Promise<void> { this.audio.pause(); }
  async resume(): Promise<void> { await this.audio.play(); }
  async stop(): Promise<void> {
    this.audio.pause();
    this.audio.currentTime = 0;
  }

  async seek(seconds: number): Promise<void> {
    this.audio.currentTime = seconds;
  }

  async setVolume(v: number): Promise<void> {
    this.audio.volume = v;
    localStorage.setItem('player_volume', String(v));
  }

  async setRate(r: number): Promise<void> { this.audio.playbackRate = r; }

  async getState(): Promise<PlaybackState> {
    return {
      state: this.audio.paused ? 'paused' : 'playing',
      position: this.audio.currentTime,
      duration: this.audio.duration || 0,
    };
  }

  async shutdown(): Promise<void> { this.audio.pause(); }

  onEvent(cb: (e: PlaybackEvent) => void): () => void {
    const handlers: Record<string, () => void> = {
      timeupdate: () => cb({
        type: 'position',
        position: this.audio.currentTime,
        duration: this.audio.duration,
      }),
      play: () => cb({ type: 'state', state: 'playing' }),
      pause: () => cb({ type: 'state', state: 'paused' }),
      ended: () => cb({ type: 'ended' }),
      error: () => cb({ type: 'error', error: 'playback failed' }),
    };
    for (const [evt, h] of Object.entries(handlers)) {
      this.audio.addEventListener(evt, h);
    }
    return () => {
      for (const [evt, h] of Object.entries(handlers)) {
        this.audio.removeEventListener(evt, h);
      }
    };
  }
}

import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';

export interface PreparedAudioSource {
  url: string;
  crossOriginSafe: boolean;
}

export interface Html5AudioBackendOptions {
  prepareSourceUrl?: (url: string) => Promise<PreparedAudioSource>;
  /** Called after audio.play() resolves — post-play attachSource (spec §5.2). */
  initEq?: (audio: HTMLAudioElement, crossOriginSafe: boolean) => void;
  /** Capture transition epoch at play start; checked after play() before initEq. */
  getAttachTransitionSeq?: () => number;
  isAttachTransitionCurrent?: (seq: number) => boolean;
  disconnectEq?: () => void;
  isEqRerouted?: () => boolean;
  setEqVolume?: (vol: number) => void;
}

export class Html5AudioBackend implements PlayerBackend {
  readonly kind = 'html5' as const;
  private lastCrossOriginSafe = false;

  constructor(
    private audio: HTMLAudioElement,
    private readonly options: Html5AudioBackendOptions = {},
  ) {
    this.audio.volume = parseFloat(localStorage.getItem('player_volume') || '0.7');
  }

  async initialize(): Promise<boolean> { return true; }

  async playUrl(url: string): Promise<boolean> {
    const attachSeq = this.options.getAttachTransitionSeq?.();
    await this.setPreparedSource(url);
    try {
      await this.audio.play();
      if (this.shouldAttachEq(attachSeq)) {
        this.options.initEq?.(this.audio, this.lastCrossOriginSafe);
      }
      return true;
    } catch (e) {
      console.warn('Html5AudioBackend playUrl play failed:', {
        url,
        error: e,
        readyState: this.audio.readyState,
        networkState: this.audio.networkState,
        mediaError: this.audio.error ? { code: this.audio.error.code, message: this.audio.error.message } : null,
        hasSrc: this.audio.hasAttribute('src'),
      });
      return false;
    }
  }

  async switchUrl(
    url: string,
    options: { position?: number; autoplay: boolean },
  ): Promise<boolean> {
    await this.setPreparedSource(url);

    if (options.position && options.position > 0) {
      await this.waitForMetadata();
      try {
        this.audio.currentTime = options.position;
      } catch {
        // Best-effort resume position. Some media reject seeks before enough
        // metadata is available; playback should still continue.
      }
    }

    if (!options.autoplay) return true;

    try {
      const attachSeq = this.options.getAttachTransitionSeq?.();
      await this.audio.play();
      if (this.shouldAttachEq(attachSeq)) {
        this.options.initEq?.(this.audio, this.lastCrossOriginSafe);
      }
      return true;
    } catch (e) {
      console.warn('Html5AudioBackend switchUrl play failed:', e);
      return false;
    }
  }

  hasSource(): boolean {
    return this.audio.hasAttribute('src') && Boolean(this.audio.getAttribute('src') || this.audio.src);
  }

  async pause(): Promise<void> { this.audio.pause(); }
  async resume(): Promise<void> { await this.audio.play(); }
  async stop(): Promise<void> {
    this.options.disconnectEq?.();
    this.audio.pause();
    this.audio.removeAttribute('src');
    this.audio.load();
  }

  async seek(seconds: number): Promise<void> {
    this.audio.currentTime = seconds;
  }

  async setVolume(v: number): Promise<void> {
    if (this.options.isEqRerouted?.()) {
      this.options.setEqVolume?.(v);
    } else {
      this.audio.volume = v;
    }
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

  private waitForMetadata(timeoutMs = 500): Promise<void> {
    if (this.audio.readyState >= 1) return Promise.resolve();

    return new Promise((resolve) => {
      let settled = false;
      const done = () => {
        if (settled) return;
        settled = true;
        this.audio.removeEventListener('loadedmetadata', done);
        window.clearTimeout(timer);
        resolve();
      };
      const timer = window.setTimeout(done, timeoutMs);
      this.audio.addEventListener('loadedmetadata', done, { once: true });
    });
  }

  private shouldAttachEq(attachSeq: number | undefined): boolean {
    if (attachSeq === undefined) return true;
    return this.options.isAttachTransitionCurrent?.(attachSeq) ?? false;
  }

  private async setPreparedSource(url: string): Promise<void> {
    const prepared = this.options.prepareSourceUrl
      ? await this.options.prepareSourceUrl(url)
      : { url, crossOriginSafe: false };

    if (prepared.crossOriginSafe) {
      this.audio.crossOrigin = 'anonymous';
    } else {
      this.audio.removeAttribute('crossorigin');
    }

    this.lastCrossOriginSafe = prepared.crossOriginSafe;
    this.audio.src = prepared.url;
  }
}

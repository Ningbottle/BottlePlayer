import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';
import type { DiagEvent } from '../playbackDiagnostics';
import type { PreparedAudioSource } from '../../shared/media/audioSource';

export interface Html5AudioBackendOptions {
  /** Starting element volume; persistence is owned by playerPersistence. */
  initialVolume?: number;
  prepareSourceUrl?: (url: string) => Promise<PreparedAudioSource>;
  /** Called after audio.play() resolves — post-play attachSource (spec §5.2). */
  initEq?: (
    audio: HTMLAudioElement,
    crossOriginSafe: boolean,
    isCurrent: () => boolean,
  ) => void | Promise<void>;
  /** Capture transition epoch at play start; checked after play() before initEq. */
  getAttachTransitionSeq?: () => number;
  isAttachTransitionCurrent?: (seq: number) => boolean;
  disconnectEq?: () => void;
  isEqRerouted?: () => boolean;
  setEqVolume?: (vol: number) => void;
  /** Records a playback diagnostic event (media_event / proxy_prep). */
  recordDiagnostic?: (e: Omit<DiagEvent, 'ts'>) => void;
}

interface SourceLease {
  readonly id: number;
}

/**
 * HTMLMediaElement.volume rejects values outside [0, 1]; fall back to the
 * historical default (0.7) for non-finite input and clamp otherwise.
 */
function normalizeInitialVolume(value: number | undefined): number {
  if (!Number.isFinite(value)) return 0.7;
  return Math.max(0, Math.min(1, value as number));
}

export class Html5AudioBackend implements PlayerBackend {
  readonly kind = 'html5' as const;
  private lastCrossOriginSafe = false;
  private sourceLeaseId = 0;

  constructor(
    private audio: HTMLAudioElement,
    private readonly options: Html5AudioBackendOptions = {},
  ) {
    this.audio.volume = normalizeInitialVolume(this.options.initialVolume);
  }

  async initialize(): Promise<boolean> { return true; }

  async playUrl(url: string): Promise<boolean> {
    const lease = this.beginSourceLease();
    const attachSeq = this.options.getAttachTransitionSeq?.();
    if (!await this.setPreparedSource(url, lease, attachSeq)) return false;
    try {
      await this.audio.play();
      if (!this.ownsPlayback(lease, attachSeq)) return false;
      if (this.shouldAttachEq(lease, attachSeq)) {
        await this.options.initEq?.(
          this.audio,
          this.lastCrossOriginSafe,
          () => this.ownsPlayback(lease, attachSeq),
        );
      }
      return this.ownsPlayback(lease, attachSeq);
    } catch (e) {
      if (!this.ownsSourceLease(lease)) return false;
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
    const lease = this.beginSourceLease();
    const attachSeq = this.options.getAttachTransitionSeq?.();
    if (!await this.setPreparedSource(url, lease, attachSeq)) return false;
    if (!this.ownsPlayback(lease, attachSeq)) return false;

    if (options.position && options.position > 0) {
      await this.waitForMetadata();
      if (!this.ownsPlayback(lease, attachSeq)) return false;
      try {
        this.audio.currentTime = options.position;
      } catch {
        // Best-effort resume position. Some media reject seeks before enough
        // metadata is available; playback should still continue.
      }
      if (!this.ownsPlayback(lease, attachSeq)) return false;
    }

    if (!options.autoplay) return this.ownsPlayback(lease, attachSeq);

    try {
      await this.audio.play();
      if (!this.ownsPlayback(lease, attachSeq)) return false;
      if (this.shouldAttachEq(lease, attachSeq)) {
        await this.options.initEq?.(
          this.audio,
          this.lastCrossOriginSafe,
          () => this.ownsPlayback(lease, attachSeq),
        );
      }
      return this.ownsPlayback(lease, attachSeq);
    } catch (e) {
      if (!this.ownsSourceLease(lease)) return false;
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
    this.beginSourceLease();
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
      ended: () => {
        this.options.recordDiagnostic?.({ kind: 'media_event', phase: 'ok', detail: 'ended' });
        cb({ type: 'ended' });
      },
      error: () => {
        const details = this.getMediaEventDetails('error');
        console.warn('Html5AudioBackend media event:', details);
        const detailStr = this.formatMediaEventDetails(details);
        this.options.recordDiagnostic?.({ kind: 'media_event', phase: 'fail', detail: detailStr });
        cb({ type: 'error', error: detailStr });
      },
      waiting: () => this.warnMediaEvent('waiting'),
      stalled: () => this.warnMediaEvent('stalled'),
      suspend: () => this.warnMediaEvent('suspend'),
      abort: () => this.warnMediaEvent('abort'),
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

  private shouldAttachEq(lease: SourceLease, attachSeq: number | undefined): boolean {
    return this.ownsPlayback(lease, attachSeq);
  }

  private ownsPlayback(lease: SourceLease, attachSeq: number | undefined): boolean {
    if (!this.ownsSourceLease(lease)) return false;
    if (attachSeq === undefined) return true;
    return this.options.isAttachTransitionCurrent?.(attachSeq) ?? false;
  }

  private beginSourceLease(): SourceLease {
    return { id: ++this.sourceLeaseId };
  }

  private ownsSourceLease(lease: SourceLease): boolean {
    return lease.id === this.sourceLeaseId;
  }

  private warnMediaEvent(event: string): void {
    const details = this.getMediaEventDetails(event);
    console.warn('Html5AudioBackend media event:', details);
    this.options.recordDiagnostic?.({
      kind: 'media_event',
      phase: 'noop',
      detail: this.formatMediaEventDetails(details),
    });
  }

  private getMediaEventDetails(event: string) {
    return {
      event,
      readyState: this.audio.readyState,
      networkState: this.audio.networkState,
      currentTime: this.audio.currentTime,
      duration: this.audio.duration,
      paused: this.audio.paused,
      ended: this.audio.ended,
      src: this.audio.currentSrc || this.audio.src || this.audio.getAttribute('src') || '',
      mediaError: this.audio.error
        ? { code: this.audio.error.code, message: this.audio.error.message }
        : null,
    };
  }

  private formatMediaEventDetails(details: ReturnType<Html5AudioBackend['getMediaEventDetails']>): string {
    const mediaError = details.mediaError
      ? `${details.mediaError.code}: ${details.mediaError.message || 'unknown'}`
      : 'none';
    return [
      `HTML5 media ${details.event}`,
      `readyState=${details.readyState}`,
      `networkState=${details.networkState}`,
      `currentTime=${details.currentTime}`,
      `duration=${details.duration}`,
      `paused=${details.paused}`,
      `ended=${details.ended}`,
      `src=${details.src || '(empty)'}`,
      `mediaError=${mediaError}`,
    ].join('; ');
  }

  private async setPreparedSource(
    url: string,
    lease: SourceLease,
    attachSeq: number | undefined,
  ): Promise<boolean> {
    let prepared: PreparedAudioSource;
    if (this.options.prepareSourceUrl) {
      try {
        prepared = await this.options.prepareSourceUrl(url);
      } catch (e) {
        this.options.recordDiagnostic?.({
          kind: 'proxy_prep',
          phase: 'fail',
          detail: `prepareSourceUrl threw: ${e instanceof Error ? e.message : String(e)}; url=${url}`,
        });
        throw e;
      }
      this.options.recordDiagnostic?.({
        kind: 'proxy_prep',
        phase: 'ok',
        detail: `prepared; crossOriginSafe=${prepared.crossOriginSafe}; url=${url}`,
      });
    } else {
      prepared = { url, crossOriginSafe: false };
    }

    if (!this.ownsPlayback(lease, attachSeq)) return false;
    this.options.disconnectEq?.();
    if (!this.ownsPlayback(lease, attachSeq)) return false;
    if (prepared.crossOriginSafe) {
      this.audio.crossOrigin = 'anonymous';
    } else {
      this.audio.removeAttribute('crossorigin');
    }

    if (!this.ownsPlayback(lease, attachSeq)) return false;
    this.lastCrossOriginSafe = prepared.crossOriginSafe;
    this.audio.src = prepared.url;
    return this.ownsPlayback(lease, attachSeq);
  }
}

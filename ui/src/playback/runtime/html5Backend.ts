import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';
import type { DiagEvent } from '../playbackDiagnostics';
import type { PreparedAudioSource } from '../../shared/media/audioSource';
import { safeGetItem, safeSetItem } from '../../platform/storage/safeStorage';

export interface Html5AudioBackendOptions {
  /** Starting element volume; persistence is owned by playerPersistence. */
  initialVolume?: number;
  prepareSourceUrl?: (url: string) => Promise<PreparedAudioSource>;
  /** Called after audio.play() resolves �?post-play attachSource (spec §5.2). */
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

/** playUrl/switchUrl false 出口的首败归因分类（2026-09-15 审阅：不能只看到
 * 「播放失败」—�?rollback 之前必须能区分失败层）�?*/
export type PlayFailureReason =
  | 'media_error'   // audio.play() 被拒且元素带�?MediaError（解�?网络/格式�?
  | 'lease_stale'   // source lease 已被 stop()/新播放接管，本次尝试就此作废
  | 'attach_stale'  // attach 过渡序号已失效（切换/取消赢得了竞争）
  | 'eq_init'       // play() 成功�?initEq 抛错
  | 'play_reject';  // play() 被拒但无 MediaError（如 NotAllowedError 自动播放策略�?

function describePlayError(e: unknown): string {
  const err = e as { name?: string; message?: string } | null;
  if (err && typeof err === 'object' && (err.name || err.message)) {
    return `${err.name ?? 'Error'}: ${err.message ?? ''}`;
  }
  return String(e);
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
  private lastPlayFailureReason: string | null = null;
  private pauseSilenceProbeHandlers: Array<{ event: string; handler: EventListener }> = [];
  private pauseSilenceDbg: ((tag: string, extra?: Record<string, unknown>) => void) | null = null;

  constructor(
    private audio: HTMLAudioElement,
    private readonly options: Html5AudioBackendOptions = {},
  ) {
    this.audio.volume = normalizeInitialVolume(this.options.initialVolume);
    this.installPauseSilenceProbe();
  }

  // TEMP-DBG: pause-silence investigation (2026-09-03). REMOVE after diagnosis.
  // Records element state around pause/resume and key media events into a
  // localStorage ring buffer so the failure can be analyzed post-hoc.
  private installPauseSilenceProbe(): void {
    const dbg = (tag: string, extra: Record<string, unknown> = {}) => {
      try {
        const rec = JSON.parse(safeGetItem('dbg_pause') || '[]') as unknown[];
        rec.push({
          t: Date.now(),
          tag,
          paused: this.audio.paused,
          vol: this.audio.volume,
          muted: this.audio.muted,
          rs: this.audio.readyState,
          ns: this.audio.networkState,
          ct: Math.round(this.audio.currentTime * 10) / 10,
          src: (this.audio.currentSrc || this.audio.src || '').slice(0, 60),
          ...extra,
        });
        safeSetItem('dbg_pause', JSON.stringify(rec.slice(-40)));
      } catch {
        /* probe must never break playback */
      }
    };
    for (const ev of [
      'play', 'playing', 'pause', 'suspend', 'stalled', 'waiting',
      'volumechange', 'emptied', 'error', 'ratechange', 'seeked',
    ]) {
      const handler: EventListener = () => dbg('ev:' + ev);
      this.audio.addEventListener(ev, handler);
      this.pauseSilenceProbeHandlers.push({ event: ev, handler });
    }
    this.pauseSilenceDbg = dbg;
    (this.audio as unknown as { __pauseSilenceDbg?: typeof dbg }).__pauseSilenceDbg = dbg;
  }

  /** Remove this backend's diagnostics hooks while leaving live playback intact. */
  dispose(): void {
    for (const { event, handler } of this.pauseSilenceProbeHandlers) {
      this.audio.removeEventListener(event, handler);
    }
    this.pauseSilenceProbeHandlers = [];
    const audioWithProbe = this.audio as unknown as {
      __pauseSilenceDbg?: (tag: string, extra?: Record<string, unknown>) => void;
    };
    if (audioWithProbe.__pauseSilenceDbg === this.pauseSilenceDbg) {
      delete audioWithProbe.__pauseSilenceDbg;
    }
    this.pauseSilenceDbg = null;
  }

  async initialize(): Promise<boolean> { return true; }

  async playUrl(url: string): Promise<boolean> {
    this.lastPlayFailureReason = null;
    const lease = this.beginSourceLease();
    const attachSeq = this.options.getAttachTransitionSeq?.();
    if (!await this.setPreparedSource(url, lease, attachSeq)) return false;
    try {
      await this.audio.play();
      if (!this.ownsPlayback(lease, attachSeq)) {
        this.recordPlayFailure(
          this.ownershipLossReason(lease),
          'playUrl: superseded after audio.play() resolved',
        );
        return false;
      }
      if (this.shouldAttachEq(lease, attachSeq)) {
        try {
          await this.options.initEq?.(
            this.audio,
            this.lastCrossOriginSafe,
            () => this.ownsPlayback(lease, attachSeq),
          );
        } catch (e) {
          if (!this.ownsSourceLease(lease)) {
            this.recordPlayFailure('lease_stale', 'playUrl: source lease superseded during initEq');
            return false;
          }
          // EQ 初始化失�?—�?单独归因，不与媒体错�?播放拒绝混淆�?
          this.recordPlayFailure('eq_init', `playUrl: initEq threw: ${describePlayError(e)}`);
          return false;
        }
      }
      if (!this.ownsPlayback(lease, attachSeq)) {
        this.recordPlayFailure(
          this.ownershipLossReason(lease),
          'playUrl: superseded during/after EQ init',
        );
        return false;
      }
      return true;
    } catch (e) {
      if (!this.ownsSourceLease(lease)) {
        this.recordPlayFailure('lease_stale', 'playUrl: source lease superseded during play');
        return false;
      }
      // 首败归因：元素带 MediaError �?media_error（解�?网络/格式）；
      // 否则 �?play_reject（如 NotAllowedError 自动播放策略）�?
      // 诊断 detail 不携�?URL（脱敏要求由 recordEvent �?redactUrls 兜底）�?
      const mediaError = this.audio.error
        ? `${this.audio.error.code}: ${this.audio.error.message || 'unknown'}`
        : null;
      if (mediaError) {
        this.recordPlayFailure(
          'media_error',
          `playUrl: play rejected with media error; mediaError=${mediaError}; `
          + `readyState=${this.audio.readyState}; networkState=${this.audio.networkState}`,
        );
      } else {
        this.recordPlayFailure(
          'play_reject',
          `playUrl: play() rejected; error=${describePlayError(e)}; `
          + `readyState=${this.audio.readyState}; networkState=${this.audio.networkState}; `
          + `hasSrc=${this.audio.hasAttribute('src')}`,
        );
      }
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

  async pause(): Promise<void> {
    // TEMP-DBG probe (remove with installPauseSilenceProbe)
    (this.audio as unknown as { __pauseSilenceDbg?: (t: string) => void })
      .__pauseSilenceDbg?.('pause()');
    this.audio.pause();
  }

  async resume(): Promise<void> {
    // TEMP-DBG probe (remove with installPauseSilenceProbe)
    const dbg = (this.audio as unknown as {
      __pauseSilenceDbg?: (t: string, e?: Record<string, unknown>) => void;
    }).__pauseSilenceDbg;
    dbg?.('resume() start');
    try {
      await this.audio.play();
      dbg?.('resume() resolved');
    } catch (e) {
      dbg?.('resume() rejected', { err: String(e) });
      throw e;
    }
  }
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

  async shutdown(): Promise<void> {
    this.dispose();
    this.audio.pause();
  }

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
      // Browsers emit suspend after a source is buffered and abort whenever
      // stop()/a newer source clears the element. They are useful in the
      // diagnostic ring, but treating these routine transitions as console
      // warnings makes every normal track switch look like a failure.
      suspend: () => this.recordTransientMediaEvent('suspend'),
      abort: () => this.recordTransientMediaEvent('abort'),
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

  /** 最近一�?playUrl/switchUrl false 出口的首败归因（无失败为 null）�?*/
  getLastPlayFailureReason(): string | null {
    return this.lastPlayFailureReason;
  }

  private recordPlayFailure(reason: PlayFailureReason, detail: string): void {
    this.lastPlayFailureReason = reason;
    this.options.recordDiagnostic?.({
      kind: 'play_fail',
      phase: 'fail',
      detail: `reason=${reason}; ${detail}`,
    });
  }

  private ownershipLossReason(lease: SourceLease): PlayFailureReason {
    // lease 失效（stop()/新播放接管）优先�?attach 过渡失效 —�?二者都�?
    // 本次尝试作废，但首败归因不同：ownsPlayback �?false 时，�?lease �?
    // 归本次尝试，则失效的一定是 attach 过渡序号�?
    return this.ownsSourceLease(lease) ? 'attach_stale' : 'lease_stale';
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
    this.recordTransientMediaEvent(event, details);
  }

  private recordTransientMediaEvent(
    event: string,
    details = this.getMediaEventDetails(event),
  ): void {
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

    if (!this.ownsPlayback(lease, attachSeq)) {
      this.recordPlayFailure(
        this.ownershipLossReason(lease),
        'setPreparedSource: superseded after source preparation',
      );
      return false;
    }
    this.options.disconnectEq?.();
    if (!this.ownsPlayback(lease, attachSeq)) {
      this.recordPlayFailure(
        this.ownershipLossReason(lease),
        'setPreparedSource: superseded after EQ disconnect',
      );
      return false;
    }
    if (prepared.crossOriginSafe) {
      this.audio.crossOrigin = 'anonymous';
    } else {
      this.audio.removeAttribute('crossorigin');
    }

    if (!this.ownsPlayback(lease, attachSeq)) {
      this.recordPlayFailure(
        this.ownershipLossReason(lease),
        'setPreparedSource: superseded before src assignment',
      );
      return false;
    }
    this.lastCrossOriginSafe = prepared.crossOriginSafe;
    this.audio.src = prepared.url;
    if (!this.ownsPlayback(lease, attachSeq)) {
      this.recordPlayFailure(
        this.ownershipLossReason(lease),
        'setPreparedSource: superseded after src assignment',
      );
      return false;
    }
    return true;
  }
}

import { normalizeTrack, type Track } from '../../api/normalizer';
import type { DiagEvent } from '../playbackDiagnostics';
import {
  canTransition,
  flagsFromPhase,
  type PlaybackPhase,
} from '../playbackPhase';
import type { ResolveTrackResult } from '../types';

export interface QualityOption {
  quality: string;
  url: string;
  fileSize?: number;
  bitRate?: number;
  extName?: string;
}

export interface PlaybackStateSlice {
  currentTrack: Track | null;
  currentIndex: number;
  queue: Track[];
  currentTime: number;
  duration: number;
  isPlaying: boolean;
  isLoading: boolean;
  errorMsg: string;
  isPreview: boolean;
  vipRequired: boolean;
  quality: string;
  availableQualities: QualityOption[];
  /** Explicit phase for UI / stability guards (see playbackPhase.ts). */
  playbackPhase: PlaybackPhase;
}

export type PlaybackResult =
  | { status: 'played' }
  | { status: 'superseded' }
  | { status: 'failed'; message: string };

interface PlaybackBackendLike {
  playUrl(url: string): Promise<boolean>;
  switchUrl(url: string, options: { position?: number; autoplay: boolean }): Promise<boolean>;
  hasSource(): boolean;
  stop(): Promise<void>;
  resume(): Promise<void>;
  seek(seconds: number): Promise<void>;
}

interface PlaySessionLike {
  skip(): void;
  intend(track: Track): void;
}

export interface PlaybackOrchestratorDeps {
  backend: () => PlaybackBackendLike;
  playSession: PlaySessionLike;
  resolveTrack: (track: Track, quality: string) => Promise<ResolveTrackResult>;
  fetchCover: (hash: string) => Promise<string | null | undefined>;
  uploadPlayHistory: (track: Track) => void;
  recordRecentPlayed: (track: Track) => void;
  recordDiagnostic: (e: Omit<DiagEvent, 'ts'>) => void;
  getState: () => PlaybackStateSlice;
  patchState: (patch: Partial<PlaybackStateSlice>) => void;
  saveQueue: () => void;
}

export class PlaybackOrchestrator {
  private transitionSeq = 0;
  private stopTail: Promise<void> = Promise.resolve();

  constructor(private readonly deps: PlaybackOrchestratorDeps) {}

  async switchTrack(track: Track): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const backend = this.deps.backend();
    const prevIndex = state.currentIndex;
    const prevTrack = prevIndex >= 0 ? state.queue[prevIndex] ?? null : null;

    this.applyPhase('resolving');
    this.deps.playSession.skip();
    await this.stopBackend(seq, backend);
    if (!this.isCurrent(seq)) return this.superseded(track, 'switchTrack stopped before start');

    const normalized = normalizeTrack(track);
    this.deps.recordDiagnostic({
      kind: 'track_switch',
      phase: 'start',
      detail: normalized.SongName || normalized.FileHash,
      trackKey: normalized.FileHash,
    });
    let idx = state.queue.findIndex((t) => t.FileHash === normalized.FileHash);
    if (idx === -1) {
      state.queue.push(normalized);
      idx = state.queue.length - 1;
    }

    this.deps.patchState({
      currentIndex: idx,
      currentTrack: normalized,
      currentTime: 0,
      duration: normalized.Duration || 0,
      errorMsg: '正在加载音频源…',
      // isPlaying/isLoading projected from playbackPhase (applyPhase above)
    });
    this.fetchMissingCover(normalized);

    let result: ResolveTrackResult;
    this.deps.recordDiagnostic({
      kind: 'url_resolve',
      phase: 'start',
      detail: normalized.FileHash,
      trackKey: normalized.FileHash,
    });
    try {
      result = await this.deps.resolveTrack(normalized, state.quality);
    } catch (err) {
      if (!this.isCurrent(seq)) return this.superseded(normalized, 'switchTrack resolve rejected');
      const error = err instanceof Error ? err.message : '获取歌曲链接失败';
      this.deps.recordDiagnostic({
        kind: 'url_resolve',
        phase: 'fail',
        detail: error,
        trackKey: normalized.FileHash,
      });
      this.rollback(prevIndex, prevTrack, error);
      return { status: 'failed', message: error };
    }
    if (!this.isCurrent(seq)) return this.superseded(normalized, 'switchTrack resolve completed');

    if (result.status !== 1 || !result.url) {
      const error = result.error || '获取歌曲链接失败';
      this.deps.recordDiagnostic({
        kind: 'url_resolve',
        phase: 'fail',
        detail: error,
        trackKey: normalized.FileHash,
      });
      this.rollback(prevIndex, prevTrack, error);
      return {
        status: 'failed',
        message: error,
      };
    }
    this.deps.recordDiagnostic({
      kind: 'url_resolve',
      phase: 'ok',
      detail: result.url,
      trackKey: normalized.FileHash,
    });

    const availableQualities = result.data?.available_qualities || [];
    let finalUrl = result.url;
    if (state.quality && availableQualities.length > 0) {
      const preferred = availableQualities.find((q) => q.quality === state.quality);
      if (preferred?.url) finalUrl = preferred.url;
    }

    this.deps.patchState({
      availableQualities,
      errorMsg: '',
      isPreview: !!result.is_preview,
      vipRequired: !!result.vip_required,
    });

    this.applyPhase('loading');
    this.deps.playSession.intend(normalized);

    let ok: boolean;
    try {
      ok = await backend.playUrl(finalUrl);
    } catch (err) {
      if (!this.isCurrent(seq)) return this.superseded(normalized, 'switchTrack play rejected');
      const message = err instanceof Error ? err.message : '播放失败';
      this.deps.playSession.skip();
      this.rollback(prevIndex, prevTrack, message);
      return { status: 'failed', message };
    }
    if (!this.isCurrent(seq)) {
      return this.superseded(normalized, 'switchTrack play completed');
    }
    if (!ok) {
      this.deps.playSession.skip();
      this.rollback(prevIndex, prevTrack, '播放失败');
      return { status: 'failed', message: '播放失败' };
    }

    this.applyPhase('playing');
    this.deps.saveQueue();
    this.deps.uploadPlayHistory(normalized);
    this.deps.recordRecentPlayed(normalized);
    return { status: 'played' };
  }

  async cancelPendingPlayback(): Promise<void> {
    const seq = this.invalidatePlaybackIntent();
    await this.stopBackend(seq, this.deps.backend());
    if (!this.isCurrent(seq)) return;

    this.deps.patchState({ errorMsg: '' });
    this.applyPhase('idle');
  }

  async clearCurrentPlayback(): Promise<void> {
    const seq = this.invalidatePlaybackIntent();
    await this.stopInvalidatedPlayback(seq);
  }

  async stopInvalidatedPlayback(seq: number): Promise<void> {
    if (!this.isCurrent(seq)) return;
    await this.stopBackend(seq, this.deps.backend());
    if (!this.isCurrent(seq)) return;

    this.deps.patchState({ errorMsg: '' });
    this.applyPhase('idle');
  }

  async switchQuality(quality: string): Promise<PlaybackResult> {
    return this.switchQualityAtPosition(quality);
  }

  private async switchQualityAtPosition(
    quality: string,
    positionOverride?: number,
    autoplayOverride?: boolean,
  ): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const current = state.currentTrack;
    if (!current) {
      return { status: 'failed', message: '没有当前歌曲' };
    }

    const position = positionOverride ?? state.currentTime;
    // Prefer phase intent: mid resolve/load still means "want to play" even if
    // flags briefly flipped while a superseded switchTrack entered resolving.
    const autoplay =
      autoplayOverride
      ?? (state.isPlaying
        || state.playbackPhase === 'playing'
        || state.playbackPhase === 'resolving'
        || state.playbackPhase === 'loading'
        || state.playbackPhase === 'recovering');
    const cached = state.availableQualities.find((q) => q.quality === quality && q.url);
    let finalUrl = cached?.url;

    if (!finalUrl) {
      let result: ResolveTrackResult;
      try {
        result = await this.deps.resolveTrack(current, quality);
      } catch (err) {
        if (!this.isCurrent(seq)) return this.superseded(current, 'switchQuality resolve rejected');
        return {
          status: 'failed',
          message: err instanceof Error ? err.message : '获取歌曲链接失败',
        };
      }
      if (!this.isCurrent(seq)) return this.superseded(current, 'switchQuality resolve completed');
      if (result.status !== 1 || !result.url) {
        return { status: 'failed', message: result.error || '获取歌曲链接失败' };
      }
      const availableQualities = result.data?.available_qualities || [];
      const preferred = availableQualities.find((q) => q.quality === quality && q.url);
      finalUrl = preferred?.url || result.url;
      this.deps.patchState({ availableQualities });
    }

    await this.waitForStops(seq);
    if (!this.isCurrent(seq)) return this.superseded(current, 'switchQuality waited for stop');

    this.deps.playSession.skip();
    this.deps.playSession.intend(current);

    let ok: boolean;
    try {
      ok = await this.deps.backend().switchUrl(finalUrl, { position, autoplay });
    } catch (err) {
      if (!this.isCurrent(seq)) return this.superseded(current, 'switchQuality switchUrl rejected');
      const message = err instanceof Error ? err.message : '播放失败';
      this.deps.playSession.skip();
      this.deps.patchState({ errorMsg: message });
      this.applyPhase('error');
      return { status: 'failed', message };
    }
    if (!this.isCurrent(seq)) return this.superseded(current, 'switchQuality switchUrl completed');
    if (!ok) {
      this.deps.playSession.skip();
      this.deps.patchState({ errorMsg: '播放失败' });
      this.applyPhase('error');
      return { status: 'failed', message: '播放失败' };
    }

    this.deps.patchState({ errorMsg: '' });
    if (autoplay) this.applyPhase('playing');
    return { status: 'played' };
  }

  async resumeOrReloadCurrent(): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const current = state.currentTrack;
    if (!current) {
      return { status: 'failed', message: '没有当前歌曲' };
    }

    const backend = this.deps.backend();
    if (!backend.hasSource()) {
      this.applyPhase('recovering');
      return this.switchTrack(current);
    }

    await this.waitForStops(seq);
    if (!this.isCurrent(seq)) return this.superseded(current, 'resume waited for stop');
    if (!backend.hasSource()) {
      this.applyPhase('recovering');
      return this.switchQualityAtPosition(state.quality, state.currentTime, true);
    }

    try {
      await backend.resume();
    } catch (err) {
      if (!this.isCurrent(seq)) return this.superseded(current, 'resume rejected');
      this.applyPhase('error');
      return {
        status: 'failed',
        message: err instanceof Error ? err.message : '恢复播放失败',
      };
    }
    if (!this.isCurrent(seq)) return this.superseded(current, 'resume completed');
    this.applyPhase('playing');
    return { status: 'played' };
  }

  async replaySameTrack(): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const current = state.currentTrack;
    if (!current) {
      return { status: 'failed', message: '没有当前歌曲' };
    }

    const backend = this.deps.backend();
    await this.waitForStops(seq);
    if (!this.isCurrent(seq)) return this.superseded(current, 'replay waited for stop');
    if (!backend.hasSource()) {
      return this.switchQualityAtPosition(state.quality, 0, true);
    }
    this.deps.playSession.intend(current);
    try {
      await backend.seek(0);
      if (!this.isCurrent(seq)) return this.superseded(current, 'replay seek completed');
      await backend.resume();
      if (!this.isCurrent(seq)) return this.superseded(current, 'replay resume completed');
      return { status: 'played' };
    } catch (err) {
      if (!this.isCurrent(seq)) return this.superseded(current, 'replay rejected');
      return {
        status: 'failed',
        message: err instanceof Error ? err.message : '重播失败',
      };
    }
  }

  /** Current transition epoch — capture before async play for post-play EQ attach guard. */
  getTransitionSeq(): number {
    return this.transitionSeq;
  }

  isTransitionCurrent(seq: number): boolean {
    return seq === this.transitionSeq;
  }

  invalidatePlaybackIntent(): number {
    this.deps.playSession.skip();
    return ++this.transitionSeq;
  }

  /**
   * Pure invalidation for HMR/module-replace: bump transitionSeq so an in-flight
   * resolve cannot commit media on the shared <audio>, WITHOUT finalizing the
   * play session as skipped (the session stays alive in the new module). Use
   * invalidatePlaybackIntent() for normal stop/supersede where stats should
   * settle; use this for detach.
   */
  detachPlaybackIntent(): number {
    return ++this.transitionSeq;
  }

  private isCurrent(seq: number): boolean {
    return this.isTransitionCurrent(seq);
  }

  private waitForStops(seq: number): Promise<void> {
    return this.stopTail.then(() => {
      if (!this.isCurrent(seq)) return;
    });
  }

  private stopBackend(seq: number, backend: PlaybackBackendLike): Promise<void> {
    const stop = this.stopTail.then(() => {
      if (!this.isCurrent(seq)) return;
      return backend.stop();
    }).catch(() => {});
    this.stopTail = stop;
    return stop;
  }

  private superseded(track: Track | null | undefined, detail: string): PlaybackResult {
    this.deps.recordDiagnostic({
      kind: 'track_switch',
      phase: 'noop',
      detail: `superseded: ${detail}`,
      trackKey: track?.FileHash,
    });
    return { status: 'superseded' };
  }

  private fetchMissingCover(track: Track): void {
    if (track.Image) return;
    this.deps.fetchCover(track.FileHash).then((image) => {
      if (!image) return;
      const state = this.deps.getState();
      if (state.currentTrack?.FileHash !== track.FileHash) return;

      state.currentTrack.Image = image;
      const queueIndex = state.queue.findIndex((item) => item.FileHash === track.FileHash);
      if (queueIndex !== -1) {
        state.queue[queueIndex].Image = image;
      }
      this.deps.saveQueue();
    }).catch(() => {});
  }

  private rollback(prevIndex: number, prevTrack: Track | null, errorMsg: string): void {
    this.deps.patchState({
      currentIndex: prevIndex,
      currentTrack: prevTrack,
      isPreview: false,
      vipRequired: false,
      errorMsg,
    });
    this.applyPhase('error');
    this.deps.saveQueue();
  }

  /**
   * Phase is authoritative: also project isPlaying/isLoading.
   * Soft-ignore illegal edges (stale races) instead of throwing.
   *
   * R1 strict: always pass `playbackPhase` in the patch so flags are derived
   * via the patch funnel. The same-phase path used to pass bare flags without
   * phase — that relied on the funnel accepting bare flag writes, which is
   * now forbidden. Including `playbackPhase: to` makes both paths go through
   * phase derivation.
   */
  private applyPhase(to: PlaybackPhase): void {
    const from = this.deps.getState().playbackPhase ?? 'idle';
    if (from !== to && !canTransition(from, to)) return;
    this.deps.patchState({
      playbackPhase: to,
      ...flagsFromPhase(to),
    });
  }
}

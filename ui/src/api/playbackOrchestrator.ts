import { normalizeTrack, type Track } from './normalizer';

export interface QualityOption {
  quality: string;
  url: string;
  fileSize?: number;
  bitRate?: number;
  extName?: string;
}

export interface ResolveTrackResult {
  status: number;
  url?: string;
  error?: string;
  is_preview?: boolean;
  vip_required?: boolean;
  data?: {
    available_qualities?: QualityOption[];
    [key: string]: unknown;
  };
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
}

export type PlaybackResult =
  | { status: 'played' }
  | { status: 'failed'; error: string }
  | { status: 'stale' };

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
  getState: () => PlaybackStateSlice;
  patchState: (patch: Partial<PlaybackStateSlice>) => void;
  saveQueue: () => void;
}

export class PlaybackOrchestrator {
  private transitionSeq = 0;
  private canceledThroughSeq = 0;

  constructor(private readonly deps: PlaybackOrchestratorDeps) {}

  async switchTrack(track: Track): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const backend = this.deps.backend();
    const prevIndex = state.currentIndex;
    const prevTrack = prevIndex >= 0 ? state.queue[prevIndex] ?? null : null;

    this.deps.playSession.skip();
    await backend.stop().catch(() => {});
    if (!this.isCurrent(seq)) return { status: 'stale' };

    const normalized = normalizeTrack(track);
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
      isPlaying: false,
      isLoading: true,
    });
    this.fetchMissingCover(normalized);

    let result: ResolveTrackResult;
    try {
      result = await this.deps.resolveTrack(normalized, state.quality);
    } catch (err) {
      if (!this.isCurrent(seq)) return { status: 'stale' };
      const error = err instanceof Error ? err.message : '获取歌曲链接失败';
      this.rollback(prevIndex, prevTrack, error);
      return { status: 'failed', error };
    }
    if (!this.isCurrent(seq)) return { status: 'stale' };

    if (result.status !== 1 || !result.url) {
      const error = result.error || '获取歌曲链接失败';
      this.rollback(prevIndex, prevTrack, error);
      return {
        status: 'failed',
        error,
      };
    }

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

    this.deps.playSession.intend(normalized);

    const ok = await backend.playUrl(finalUrl);
    if (!this.isCurrent(seq)) {
      await this.cleanupCanceledStaleTransition(seq, backend);
      return { status: 'stale' };
    }
    if (!ok) {
      this.deps.playSession.skip();
      this.rollback(prevIndex, prevTrack, '播放失败');
      return { status: 'failed', error: '播放失败' };
    }

    this.deps.patchState({ isLoading: false });
    this.deps.saveQueue();
    this.deps.uploadPlayHistory(normalized);
    this.deps.recordRecentPlayed(normalized);
    return { status: 'played' };
  }

  async cancelPendingPlayback(): Promise<void> {
    const pendingSeq = this.transitionSeq;
    this.canceledThroughSeq = Math.max(this.canceledThroughSeq, pendingSeq);
    const seq = ++this.transitionSeq;

    this.deps.playSession.skip();
    await this.deps.backend().stop().catch(() => {});
    if (!this.isCurrent(seq)) return;

    this.deps.patchState({
      isLoading: false,
      isPlaying: false,
      errorMsg: '',
    });
  }

  async switchQuality(quality: string): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    const current = state.currentTrack;
    if (!current) {
      return { status: 'failed', error: '没有当前歌曲' };
    }

    const position = state.currentTime;
    const autoplay = state.isPlaying;
    const cached = state.availableQualities.find((q) => q.quality === quality && q.url);
    let finalUrl = cached?.url;

    if (!finalUrl) {
      let result: ResolveTrackResult;
      try {
        result = await this.deps.resolveTrack(current, quality);
      } catch (err) {
        if (!this.isCurrent(seq)) return { status: 'stale' };
        return {
          status: 'failed',
          error: err instanceof Error ? err.message : '获取歌曲链接失败',
        };
      }
      if (!this.isCurrent(seq)) return { status: 'stale' };
      if (result.status !== 1 || !result.url) {
        return { status: 'failed', error: result.error || '获取歌曲链接失败' };
      }
      const availableQualities = result.data?.available_qualities || [];
      const preferred = availableQualities.find((q) => q.quality === quality && q.url);
      finalUrl = preferred?.url || result.url;
      this.deps.patchState({ availableQualities });
    }

    this.deps.playSession.skip();
    this.deps.playSession.intend(current);

    const ok = await this.deps.backend().switchUrl(finalUrl, { position, autoplay });
    if (!this.isCurrent(seq)) return { status: 'stale' };
    if (!ok) {
      this.deps.playSession.skip();
      this.deps.patchState({ errorMsg: '播放失败', isPlaying: false });
      return { status: 'failed', error: '播放失败' };
    }

    this.deps.patchState({ errorMsg: '' });
    return { status: 'played' };
  }

  async resumeOrReloadCurrent(): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    if (!state.currentTrack) {
      return { status: 'failed', error: '没有当前歌曲' };
    }

    const backend = this.deps.backend();
    if (!backend.hasSource()) {
      return this.switchTrack(state.currentTrack);
    }

    try {
      await backend.resume();
    } catch (err) {
      if (!this.isCurrent(seq)) return { status: 'stale' };
      return {
        status: 'failed',
        error: err instanceof Error ? err.message : '恢复播放失败',
      };
    }
    if (!this.isCurrent(seq)) return { status: 'stale' };
    return { status: 'played' };
  }

  async replaySameTrack(): Promise<PlaybackResult> {
    const seq = ++this.transitionSeq;
    const state = this.deps.getState();
    if (!state.currentTrack) {
      return { status: 'failed', error: '没有当前歌曲' };
    }

    const backend = this.deps.backend();
    this.deps.playSession.intend(state.currentTrack);
    try {
      await backend.seek(0);
      if (!this.isCurrent(seq)) return { status: 'stale' };
      await backend.resume();
      if (!this.isCurrent(seq)) return { status: 'stale' };
      return { status: 'played' };
    } catch (err) {
      if (!this.isCurrent(seq)) return { status: 'stale' };
      return {
        status: 'failed',
        error: err instanceof Error ? err.message : '重播失败',
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

  private isCurrent(seq: number): boolean {
    return this.isTransitionCurrent(seq);
  }

  private async cleanupCanceledStaleTransition(
    seq: number,
    backend: PlaybackBackendLike,
  ): Promise<void> {
    if (seq > this.canceledThroughSeq) return;
    if (this.transitionSeq !== this.canceledThroughSeq + 1) return;

    await backend.stop().catch(() => {});
    if (this.transitionSeq !== this.canceledThroughSeq + 1) return;

    this.deps.patchState({
      isLoading: false,
      isPlaying: false,
      errorMsg: '',
    });
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
      isPlaying: false,
      isLoading: false,
      isPreview: false,
      vipRequired: false,
      errorMsg,
    });
    this.deps.saveQueue();
  }
}

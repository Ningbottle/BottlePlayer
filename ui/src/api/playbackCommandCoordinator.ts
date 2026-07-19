/**
 * PlaybackCommandCoordinator — single entry for all playback intents.
 *
 * Coalescing (NOT simple FIFO):
 * - next/prev: relative delta merge
 * - selectTrack / seek: latest-wins
 * - clearQueue: barrier (drops pending nav/select/seek/ended/quality)
 * - removeTrack: strict serial FIFO among removes
 * - ended: once per playback epoch
 * - switchQuality: exclusive transaction when drained
 * - togglePlay: after current track intent settles
 */

import { normalizeTrack, type Track } from './normalizer';
import { flagsFromPhase, type PlaybackPhase } from './playbackPhase';
import type { QualityOption } from './playbackOrchestrator';

export type LoopMode = 'list' | 'single' | 'random';
export type QueueMode = 'normal' | 'personalFm';

export type PlaybackCommand =
  | { type: 'next' }
  | { type: 'prev' }
  | { type: 'selectTrack'; track: Track }
  | { type: 'seek'; seconds: number }
  | { type: 'togglePlay' }
  | { type: 'switchQuality'; quality: string }
  | { type: 'clearQueue' }
  | { type: 'removeTrack'; index: number }
  | { type: 'ended' }
  | { type: 'playAll'; tracks: Track[]; startIndex?: number; queueMode?: QueueMode }
  | { type: 'addToQueue'; track: Track };

export type PlaybackCommandResult = {
  status: 'ok' | 'superseded' | 'failed' | 'noop';
  message?: string;
};

export type CoordinatorState = {
  queue: Track[];
  currentIndex: number;
  currentTrack: Track | null;
  isPlaying: boolean;
  isLoading: boolean;
  currentTime: number;
  duration: number;
  errorMsg: string;
  isPreview: boolean;
  vipRequired: boolean;
  availableQualities: QualityOption[];
  playbackPhase: PlaybackPhase;
  queueMode: QueueMode;
  loopMode: LoopMode;
  audio: HTMLAudioElement | null;
};

export type CoordinatorDeps = {
  getState: () => CoordinatorState;
  patchState: (patch: Partial<CoordinatorState>) => void;
  saveQueue: () => void;
  /** Start playing a track (orchestrator switchTrack). */
  playTrack: (track: Track) => Promise<{ status: string; message?: string } | void>;
  switchQuality: (quality: string) => Promise<{ status: string; message?: string } | void>;
  seek: (seconds: number) => Promise<void>;
  pause: () => Promise<void>;
  resumeOrReload: () => Promise<unknown>;
  invalidatePlaybackIntent: () => number;
  stopInvalidatedPlayback: (seq: number) => Promise<void>;
  skipSession: () => void;
  hasBackend: () => boolean;
  /** Optional: personal FM append when at end of personalFm queue. */
  appendPersonalFm?: () => Promise<boolean>;
};

type Waiter = {
  resolve: (r: PlaybackCommandResult) => void;
};

function clearResiduals(_state: CoordinatorState, patch: CoordinatorDeps['patchState']): void {
  patch({
    errorMsg: '',
    currentTime: 0,
    duration: 0,
    isPreview: false,
    vipRequired: false,
    availableQualities: [],
    playbackPhase: 'idle',
    ...flagsFromPhase('idle'),
  });
}

export class PlaybackCommandCoordinator {
  private deps: CoordinatorDeps;
  private disposed = false;
  private draining = false;

  /** Merged next(+1)/prev(-1) displacement. */
  private navDelta = 0;
  private pendingSelect: Track | null = null;
  private pendingSeek: number | null = null;
  private pendingToggle = false;
  private pendingQuality: string | null = null;
  private pendingClear = false;
  private pendingEnded = false;
  private pendingPlayAll: {
    tracks: Track[];
    startIndex: number;
    queueMode: QueueMode;
  } | null = null;
  private pendingAdds: Track[] = [];
  private pendingRemoves: number[] = [];

  /** Waiters for the current coalesce generation (resolved when drain finishes a batch). */
  private waiters: Waiter[] = [];

  /** Playback epoch — bumped on clear and successful track commit. */
  private epoch = 0;
  /** Epoch for which ended already advanced. */
  private endedEpochHandled = -1;
  /** Interrupt in-flight play/nav so a newer select/nav/clear can start immediately. */
  private interruptPlay: (() => void) | null = null;

  constructor(deps: CoordinatorDeps) {
    this.deps = deps;
  }

  private bumpInterrupt(): void {
    const fn = this.interruptPlay;
    this.interruptPlay = null;
    if (fn) fn();
  }

  getEpoch(): number {
    return this.epoch;
  }

  async dispatch(command: PlaybackCommand): Promise<PlaybackCommandResult> {
    if (this.disposed) {
      return { status: 'failed', message: 'coordinator_disposed' };
    }

    // ended already handled for this epoch (including mid-flight) → immediate noop
    if (
      command.type === 'ended'
      && this.endedEpochHandled === this.epoch
      && !this.pendingEnded
    ) {
      return { status: 'noop' };
    }

    return new Promise<PlaybackCommandResult>((resolve) => {
      this.waiters.push({ resolve });
      this.ingest(command);
      void this.scheduleDrain();
    });
  }

  async dispose(): Promise<void> {
    this.disposed = true;
    this.pendingClear = true;
    this.navDelta = 0;
    this.pendingSelect = null;
    this.pendingSeek = null;
    this.pendingToggle = false;
    this.pendingQuality = null;
    this.pendingEnded = false;
    this.pendingPlayAll = null;
    this.pendingAdds = [];
    this.pendingRemoves = [];
    await this.scheduleDrain();
  }

  private ingest(command: PlaybackCommand): void {
    switch (command.type) {
      case 'clearQueue':
        this.pendingClear = true;
        // Barrier: drop uncommitted intents.
        this.navDelta = 0;
        this.pendingSelect = null;
        this.pendingSeek = null;
        this.pendingToggle = false;
        this.pendingQuality = null;
        this.pendingEnded = false;
        this.pendingPlayAll = null;
        this.pendingAdds = [];
        this.pendingRemoves = [];
        this.bumpInterrupt();
        break;
      case 'next':
        if (this.pendingClear) break;
        this.pendingSelect = null; // nav and select compete: nav after select clears select
        this.navDelta += 1;
        this.bumpInterrupt();
        break;
      case 'prev':
        if (this.pendingClear) break;
        this.pendingSelect = null;
        this.navDelta -= 1;
        this.bumpInterrupt();
        break;
      case 'selectTrack':
        if (this.pendingClear) break;
        this.pendingSelect = normalizeTrack(command.track);
        this.navDelta = 0; // latest select replaces pending navigation
        this.bumpInterrupt();
        break;
      case 'seek':
        if (this.pendingClear) break;
        this.pendingSeek = command.seconds;
        break;
      case 'togglePlay':
        if (this.pendingClear) break;
        this.pendingToggle = !this.pendingToggle;
        // Cancel in-flight load (toggle-while-loading) without waiting for playUrl.
        if (this.interruptPlay) this.bumpInterrupt();
        break;
      case 'switchQuality':
        if (this.pendingClear) break;
        this.pendingQuality = command.quality;
        break;
      case 'removeTrack':
        if (this.pendingClear) break;
        this.pendingRemoves.push(command.index);
        break;
      case 'ended':
        if (this.pendingClear) break;
        if (this.endedEpochHandled === this.epoch) break;
        this.pendingEnded = true;
        break;
      case 'playAll':
        if (this.pendingClear) break;
        this.pendingPlayAll = {
          tracks: command.tracks.map(normalizeTrack),
          startIndex: command.startIndex ?? 0,
          queueMode: command.queueMode ?? 'normal',
        };
        this.navDelta = 0;
        this.pendingSelect = null;
        this.pendingEnded = false;
        break;
      case 'addToQueue':
        if (this.pendingClear) break;
        this.pendingAdds.push(normalizeTrack(command.track));
        break;
      default:
        break;
    }
  }

  private async scheduleDrain(): Promise<void> {
    if (this.draining) return;
    this.draining = true;
    try {
      // Loop until mailbox empty (new commands may arrive during await).
      while (!this.disposed && this.hasWork()) {
        await this.drainOnce();
      }
      // If disposed with clear, one more drain for stop.
      if (this.disposed && this.pendingClear) {
        await this.drainOnce();
      }
    } finally {
      this.draining = false;
      // If work arrived after we checked, schedule again.
      if (!this.disposed && this.hasWork()) {
        void this.scheduleDrain();
      }
    }
  }

  private hasWork(): boolean {
    return (
      this.pendingClear
      || this.pendingPlayAll != null
      || this.pendingRemoves.length > 0
      || this.pendingAdds.length > 0
      || this.pendingSelect != null
      || this.navDelta !== 0
      || this.pendingSeek != null
      || this.pendingToggle
      || this.pendingQuality != null
      || this.pendingEnded
    );
  }

  private takeWaiters(): Waiter[] {
    const w = this.waiters;
    this.waiters = [];
    return w;
  }

  private resolveWaiters(waiters: Waiter[], result: PlaybackCommandResult): void {
    for (const w of waiters) w.resolve(result);
  }

  private async drainOnce(): Promise<void> {
    const batchWaiters = this.takeWaiters();

    // 1) Barrier clear
    if (this.pendingClear) {
      this.pendingClear = false;
      const seq = this.deps.invalidatePlaybackIntent();
      this.deps.skipSession();
      this.deps.patchState({
        queue: [],
        currentIndex: -1,
        currentTrack: null,
      });
      clearResiduals(this.deps.getState(), this.deps.patchState);
      if (this.deps.hasBackend()) {
        await this.deps.stopInvalidatedPlayback(seq);
      } else {
        const audio = this.deps.getState().audio;
        if (audio) {
          try {
            audio.pause();
            audio.src = '';
          } catch {
            /* ignore */
          }
        }
      }
      this.deps.saveQueue();
      this.epoch += 1;
      this.endedEpochHandled = this.epoch;
      this.resolveWaiters(batchWaiters, { status: 'ok' });
      return;
    }

    // 2) playAll / replace queue
    if (this.pendingPlayAll) {
      const job = this.pendingPlayAll;
      this.pendingPlayAll = null;
      const start = Math.max(0, Math.min(job.startIndex, Math.max(0, job.tracks.length - 1)));
      this.deps.patchState({
        queue: job.tracks,
        currentIndex: job.tracks.length ? start : -1,
        queueMode: job.queueMode,
      });
      this.deps.saveQueue();
      if (job.tracks.length) {
        const r = await this.playInterruptible(job.tracks[start]);
        if (r.status === 'ok') {
          this.epoch += 1;
          this.endedEpochHandled = -1;
        }
        this.resolveWaiters(batchWaiters, r);
        return;
      }
      this.resolveWaiters(batchWaiters, { status: 'ok' });
      return;
    }

    // 3) Adds
    if (this.pendingAdds.length) {
      const adds = this.pendingAdds;
      this.pendingAdds = [];
      const state = this.deps.getState();
      const queue = [...state.queue];
      for (const t of adds) {
        if (!queue.some((q) => q.FileHash === t.FileHash)) queue.push(t);
      }
      this.deps.patchState({ queue });
      this.deps.saveQueue();
    }

    // 4) Removes (serial, one per drain step for clarity — process all in order)
    if (this.pendingRemoves.length) {
      const index = this.pendingRemoves.shift()!;
      await this.applyRemove(index);
      this.resolveWaiters(batchWaiters, { status: 'ok' });
      return;
    }

    // 5) Select track (latest-wins already applied in mailbox)
    if (this.pendingSelect) {
      const track = this.pendingSelect;
      this.pendingSelect = null;
      this.navDelta = 0;
      const r = await this.playInterruptible(track);
      if (r.status === 'ok') {
        this.epoch += 1;
        this.endedEpochHandled = -1;
      }
      this.resolveWaiters(batchWaiters, r);
      return;
    }

    // 6) Relative navigation
    if (this.navDelta !== 0) {
      const delta = this.navDelta;
      this.navDelta = 0;
      const r = await this.applyNav(delta);
      if (r.status === 'ok') {
        this.epoch += 1;
        this.endedEpochHandled = -1;
      }
      this.resolveWaiters(batchWaiters, r);
      return;
    }

    // 7) Seek latest
    if (this.pendingSeek != null) {
      const sec = this.pendingSeek;
      this.pendingSeek = null;
      await this.deps.seek(sec);
      this.deps.patchState({ currentTime: sec });
      this.resolveWaiters(batchWaiters, { status: 'ok' });
      return;
    }

    // 8) Quality transaction
    if (this.pendingQuality != null) {
      const q = this.pendingQuality;
      this.pendingQuality = null;
      const before = snapshotPlayback(this.deps.getState());
      try {
        const r = await this.deps.switchQuality(q);
        if (r && typeof r === 'object' && r.status === 'failed') {
          restorePlayback(this.deps, before);
          this.resolveWaiters(batchWaiters, {
            status: 'failed',
            message: r.message || 'quality_switch_failed',
          });
          return;
        }
        if (r && typeof r === 'object' && r.status === 'superseded') {
          this.resolveWaiters(batchWaiters, { status: 'superseded' });
          return;
        }
        this.resolveWaiters(batchWaiters, { status: 'ok' });
      } catch (e) {
        restorePlayback(this.deps, before);
        this.resolveWaiters(batchWaiters, {
          status: 'failed',
          message: e instanceof Error ? e.message : 'quality_switch_failed',
        });
      }
      return;
    }

    // 9) Toggle play
    if (this.pendingToggle) {
      this.pendingToggle = false;
      const state = this.deps.getState();
      if (!state.currentTrack) {
        this.resolveWaiters(batchWaiters, { status: 'noop' });
        return;
      }
      if (state.isLoading) {
        // cancel handled by resume path consumers; treat as pause intent
        const seq = this.deps.invalidatePlaybackIntent();
        await this.deps.stopInvalidatedPlayback(seq);
        this.deps.patchState({
          playbackPhase: 'paused',
          ...flagsFromPhase('paused'),
        });
        this.resolveWaiters(batchWaiters, { status: 'ok' });
        return;
      }
      if (state.isPlaying) {
        await this.deps.pause();
        this.deps.patchState({
          playbackPhase: 'paused',
          ...flagsFromPhase('paused'),
        });
      } else {
        await this.deps.resumeOrReload();
      }
      this.resolveWaiters(batchWaiters, { status: 'ok' });
      return;
    }

    // 10) Ended (once per epoch) — mark epoch handled before await so late ended no-ops
    if (this.pendingEnded) {
      this.pendingEnded = false;
      if (this.endedEpochHandled === this.epoch) {
        this.resolveWaiters(batchWaiters, { status: 'noop' });
        return;
      }
      const epochAtStart = this.epoch;
      this.endedEpochHandled = epochAtStart;
      const r = await this.applyNav(1, /* fromEnded */ true);
      if (r.status === 'ok') {
        this.epoch = epochAtStart + 1;
        // New epoch accepts a future ended; do not mark it handled yet.
        if (this.endedEpochHandled === epochAtStart) {
          this.endedEpochHandled = -1;
        }
      }
      this.resolveWaiters(batchWaiters, r);
      return;
    }

    this.resolveWaiters(batchWaiters, { status: 'noop' });
  }

  /**
   * Run playTrack but allow a newer select/nav/clear to interrupt the wait so the
   * orchestrator can start the new track immediately (transitionSeq supersede).
   */
  private async playInterruptible(
    track: Track,
  ): Promise<PlaybackCommandResult> {
    const playPromise = this.deps.playTrack(track);
    let interrupted = false;
    const interruptPromise = new Promise<PlaybackCommandResult>((resolve) => {
      this.interruptPlay = () => {
        interrupted = true;
        resolve({ status: 'superseded' });
      };
    });
    try {
      const result = await Promise.race([
        playPromise.then((r) => mapPlayResult(r)),
        interruptPromise,
      ]);
      if (interrupted) {
        // Keep playPromise running; a newer drain step starts another playTrack.
        void playPromise.catch(() => {});
      }
      return result;
    } finally {
      if (this.interruptPlay) this.interruptPlay = null;
    }
  }

  private async applyNav(delta: number, fromEnded = false): Promise<PlaybackCommandResult> {
    const state = this.deps.getState();
    if (!state.queue.length) return { status: 'noop' };

    let idx = state.currentIndex;
    if (idx < 0 || idx >= state.queue.length) {
      idx = state.currentTrack
        ? state.queue.findIndex((t) => t.FileHash === state.currentTrack!.FileHash)
        : 0;
      if (idx < 0) idx = 0;
    }

    const loop = state.loopMode;
    const mode = state.queueMode;

    if (fromEnded && loop === 'single') {
      const track = state.queue[idx];
      if (!track) return { status: 'noop' };
      return this.playInterruptible(track);
    }

    let nextIdx = idx;
    if (loop === 'random' && state.queue.length > 1) {
      // Apply |delta| random steps for coalesced next/prev storms.
      const steps = Math.abs(delta) || 1;
      for (let s = 0; s < steps; s++) {
        let pick = Math.floor(Math.random() * state.queue.length);
        if (state.queue.length > 1) {
          while (pick === nextIdx) pick = Math.floor(Math.random() * state.queue.length);
        }
        nextIdx = pick;
      }
    } else {
      nextIdx = idx + delta;
      if (mode === 'personalFm' && nextIdx >= state.queue.length) {
        if (this.deps.appendPersonalFm) {
          const ok = await this.deps.appendPersonalFm();
          if (!ok) return { status: 'noop', message: 'fm_exhausted' };
          // re-read queue after append
          const q = this.deps.getState().queue;
          nextIdx = Math.min(idx + delta, q.length - 1);
          if (nextIdx < 0 || nextIdx >= q.length) return { status: 'noop' };
          return this.playInterruptible(q[nextIdx]);
        }
        return { status: 'noop' };
      }
      if (nextIdx < 0) nextIdx = ((nextIdx % state.queue.length) + state.queue.length) % state.queue.length;
      else nextIdx = nextIdx % state.queue.length;
    }

    const q = this.deps.getState().queue;
    if (!q.length || nextIdx < 0 || nextIdx >= q.length) return { status: 'noop' };
    return this.playInterruptible(q[nextIdx]);
  }

  private async applyRemove(index: number): Promise<void> {
    const state = this.deps.getState();
    if (index < 0 || index >= state.queue.length) return;

    const queue = [...state.queue];
    queue.splice(index, 1);

    if (state.currentIndex === index) {
      if (queue.length === 0) {
        const seq = this.deps.invalidatePlaybackIntent();
        this.deps.skipSession();
        this.deps.patchState({
          queue: [],
          currentIndex: -1,
          currentTrack: null,
        });
        clearResiduals(this.deps.getState(), this.deps.patchState);
        if (this.deps.hasBackend()) {
          await this.deps.stopInvalidatedPlayback(seq);
        } else if (state.audio) {
          try {
            state.audio.pause();
            state.audio.src = '';
          } catch {
            /* ignore */
          }
        }
        this.epoch += 1;
        this.endedEpochHandled = this.epoch;
      } else {
        const nextIndex = index % queue.length;
        this.deps.patchState({ queue, currentIndex: nextIndex });
        this.deps.saveQueue();
        await this.playInterruptible(queue[nextIndex]);
        this.epoch += 1;
        this.endedEpochHandled = -1;
        return;
      }
    } else {
      let currentIndex = state.currentIndex;
      if (currentIndex > index) currentIndex -= 1;
      this.deps.patchState({ queue, currentIndex });
    }
    this.deps.saveQueue();
  }
}

function mapPlayResult(
  r: { status: string; message?: string } | void,
): PlaybackCommandResult {
  if (!r) return { status: 'ok' };
  if (r.status === 'played' || r.status === 'ok') return { status: 'ok' };
  if (r.status === 'superseded') return { status: 'superseded' };
  if (r.status === 'failed') return { status: 'failed', message: r.message };
  return { status: 'ok' };
}

type PlaybackSnapshot = {
  currentTime: number;
  duration: number;
  isPlaying: boolean;
  isLoading: boolean;
  errorMsg: string;
  isPreview: boolean;
  vipRequired: boolean;
  availableQualities: QualityOption[];
  playbackPhase: PlaybackPhase;
  currentTrack: Track | null;
  currentIndex: number;
};

function snapshotPlayback(state: CoordinatorState): PlaybackSnapshot {
  return {
    currentTime: state.currentTime,
    duration: state.duration,
    isPlaying: state.isPlaying,
    isLoading: state.isLoading,
    errorMsg: state.errorMsg,
    isPreview: state.isPreview,
    vipRequired: state.vipRequired,
    availableQualities: [...state.availableQualities],
    playbackPhase: state.playbackPhase,
    currentTrack: state.currentTrack,
    currentIndex: state.currentIndex,
  };
}

function restorePlayback(deps: CoordinatorDeps, snap: PlaybackSnapshot): void {
  // Phase is source of truth; flags re-derived (snapshot flags kept for tests that
  // read before phase projection if patch does not reproject).
  deps.patchState({
    currentTime: snap.currentTime,
    duration: snap.duration,
    errorMsg: snap.errorMsg,
    isPreview: snap.isPreview,
    vipRequired: snap.vipRequired,
    availableQualities: snap.availableQualities,
    playbackPhase: snap.playbackPhase,
    ...flagsFromPhase(snap.playbackPhase),
    currentTrack: snap.currentTrack,
    currentIndex: snap.currentIndex,
  });
}

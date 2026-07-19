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
 *
 * Each dispatch Promise is bound to its intent ticket — settling seek must
 * never resolve a pending quality/select Promise.
 */

import { normalizeTrack, type Track } from './normalizer';
import { flagsFromPhase, type PlaybackPhase } from './playbackPhase';
import type { QualityOption } from './playbackOrchestrator';
import type { PersonalFmAppendOptions, PersonalFmAppendSuccess } from './fmSession';

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
  /** Pure invalidation (no play-session skip) for HMR/module-replace detach. */
  detachPlaybackIntent: () => number;
  stopInvalidatedPlayback: (seq: number) => Promise<void>;
  skipSession: () => void;
  hasBackend: () => boolean;
  /** Optional: personal FM append when at end of personalFm queue. */
  appendPersonalFm?: (options?: PersonalFmAppendOptions) => Promise<boolean>;
};

type Waiter = {
  resolve: (r: PlaybackCommandResult) => void;
};

type RemoveJob = {
  index: number;
  waiter: Waiter;
};

type FmRecoveryIntent = {
  queueRef: Track[];
  currentTrack: Track | null;
  trackKey: string;
  currentIndex: number;
  queueLength: number;
  delta: number;
  epoch: number;
  generation: number;
  appendedCount: number;
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

function takeWaiters(bucket: Waiter[]): Waiter[] {
  const w = bucket.splice(0, bucket.length);
  return w;
}

function resolveWaiters(waiters: Waiter[], result: PlaybackCommandResult): void {
  for (const w of waiters) w.resolve(result);
}

function failMessage(e: unknown, fallback: string): string {
  return e instanceof Error ? e.message : fallback;
}

function failedResult(e: unknown, fallback: string): PlaybackCommandResult {
  return { status: 'failed', message: failMessage(e, fallback) };
}

export class PlaybackCommandCoordinator {
  private deps: CoordinatorDeps;
  private disposed = false;

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
  private pendingFmRecovery: FmRecoveryIntent | null = null;
  private pendingAdds: Track[] = [];
  private pendingRemoves: RemoveJob[] = [];

  /** Per-intent waiter buckets — never share across command kinds. */
  private clearWaiters: Waiter[] = [];
  private playAllWaiters: Waiter[] = [];
  private selectWaiters: Waiter[] = [];
  private navWaiters: Waiter[] = [];
  private seekWaiters: Waiter[] = [];
  private qualityWaiters: Waiter[] = [];
  private toggleWaiters: Waiter[] = [];
  private endedWaiters: Waiter[] = [];
  private addWaiters: Waiter[] = [];

  /** Playback epoch — bumped on clear and successful track commit. */
  private epoch = 0;
  /** Epoch for which ended already advanced. */
  private endedEpochHandled = -1;
  /** Interrupt in-flight play/nav so a newer select/nav/clear can start immediately. */
  private interruptPlay: (() => void) | null = null;

  /** Serialize drain so dispose() can await in-flight work. */
  private drainTail: Promise<void> = Promise.resolve();

  constructor(deps: CoordinatorDeps) {
    this.deps = deps;
  }

  private bumpInterrupt(): void {
    const fn = this.interruptPlay;
    this.interruptPlay = null;
    if (fn) fn();
  }

  private queueFmRecovery(
    base: Omit<FmRecoveryIntent, 'generation' | 'appendedCount'>,
    result: PersonalFmAppendSuccess,
  ): void {
    if (this.disposed || result.queueRef !== base.queueRef || result.appendedCount <= 0) return;
    this.pendingFmRecovery = {
      ...base,
      generation: result.generation,
      appendedCount: result.appendedCount,
    };
    void this.scheduleDrain();
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
      this.ingest(command, { resolve });
      void this.scheduleDrain();
    });
  }

  /**
   * Explicit clear-queue barrier stop. Prefer `shutdown()` for app exit so the
   * persisted queue is not wiped.
   */
  async dispose(): Promise<void> {
    if (this.disposed) {
      await this.drainTail;
      return;
    }
    this.disposed = true;
    this.bumpInterrupt();
    this.supersedeMailbox({ status: 'superseded', message: 'coordinator_disposed' });
    this.pendingClear = true;
    // No external clear waiter; drain still runs barrier stop.
    await this.scheduleDrain();
  }

  /**
   * App exit / pagehide: supersede intents and stop media WITHOUT clearing the
   * queue (so localStorage can keep the last session).
   */
  async shutdown(): Promise<void> {
    if (this.disposed) {
      await this.drainTail;
      return;
    }
    this.disposed = true;
    this.bumpInterrupt();
    this.supersedeMailbox({ status: 'superseded', message: 'coordinator_shutdown' });
    this.pendingClear = false;
    this.pendingPlayAll = null;
    this.navDelta = 0;
    this.pendingSelect = null;
    this.pendingSeek = null;
    this.pendingToggle = false;
    this.pendingQuality = null;
    this.pendingEnded = false;
    this.pendingAdds = [];
    this.pendingRemoves = [];
    resolveWaiters(takeWaiters(this.clearWaiters), {
      status: 'superseded',
      message: 'coordinator_shutdown',
    });
    try {
      const seq = this.deps.invalidatePlaybackIntent();
      if (this.deps.hasBackend()) {
        await this.deps.stopInvalidatedPlayback(seq);
      }
    } catch {
      /* best-effort stop */
    }
    await this.drainTail;
  }

  /**
   * HMR / module-replace: drop the mailbox and stop accepting commands without
   * running clear barrier, pause, or emptying the shared <audio> element.
   *
   * Also invalidates the orchestrator transitionSeq so an in-flight resolve
   * cannot proceed to playUrl on the shared element after this module dies.
   */
  async detach(): Promise<void> {
    if (this.disposed) {
      await this.drainTail;
      return;
    }
    this.disposed = true;
    this.bumpInterrupt();
    // Pure invalidate — no stop — so late switchTrack cannot commit media.
    try {
      this.deps.detachPlaybackIntent();
    } catch {
      /* ignore */
    }
    this.supersedeMailbox({ status: 'superseded', message: 'coordinator_detached' });
    // Drop any barrier that was mid-flight so drain does not stop media.
    this.pendingClear = false;
    resolveWaiters(takeWaiters(this.clearWaiters), {
      status: 'superseded',
      message: 'coordinator_detached',
    });
    // Let any in-flight drainOnce finish (interrupted plays no-op on waiters).
    await this.drainTail;
  }

  private supersedeMailbox(result: PlaybackCommandResult): void {
    this.navDelta = 0;
    this.pendingSelect = null;
    this.pendingSeek = null;
    this.pendingToggle = false;
    this.pendingQuality = null;
    this.pendingEnded = false;
    this.pendingPlayAll = null;
    this.pendingFmRecovery = null;
    this.pendingAdds = [];
    const removes = this.pendingRemoves.splice(0);
    resolveWaiters(takeWaiters(this.navWaiters), result);
    resolveWaiters(takeWaiters(this.selectWaiters), result);
    resolveWaiters(takeWaiters(this.seekWaiters), result);
    resolveWaiters(takeWaiters(this.qualityWaiters), result);
    resolveWaiters(takeWaiters(this.toggleWaiters), result);
    resolveWaiters(takeWaiters(this.endedWaiters), result);
    resolveWaiters(takeWaiters(this.playAllWaiters), result);
    resolveWaiters(takeWaiters(this.addWaiters), result);
    for (const job of removes) job.waiter.resolve(result);
  }

  private ingest(command: PlaybackCommand, waiter: Waiter): void {
    switch (command.type) {
      case 'clearQueue':
        // Barrier cancels only intents already in the mailbox (pre-clear).
        // Commands submitted after this clear remain queued and run post-stop.
        this.bumpInterrupt();
        this.supersedeMailbox({ status: 'superseded', message: 'cleared' });
        this.pendingClear = true;
        this.clearWaiters.push(waiter);
        break;
      case 'next':
        this.pendingFmRecovery = null;
        if (this.pendingSelect) {
          this.pendingSelect = null;
          resolveWaiters(takeWaiters(this.selectWaiters), {
            status: 'superseded',
            message: 'nav',
          });
        }
        this.navDelta += 1;
        this.navWaiters.push(waiter);
        // Interrupt only when not waiting on a clear barrier (post-clear nav waits).
        if (!this.pendingClear) this.bumpInterrupt();
        break;
      case 'prev':
        this.pendingFmRecovery = null;
        if (this.pendingSelect) {
          this.pendingSelect = null;
          resolveWaiters(takeWaiters(this.selectWaiters), {
            status: 'superseded',
            message: 'nav',
          });
        }
        this.navDelta -= 1;
        this.navWaiters.push(waiter);
        if (!this.pendingClear) this.bumpInterrupt();
        break;
      case 'selectTrack':
        this.pendingFmRecovery = null;
        if (this.navDelta !== 0) {
          this.navDelta = 0;
          resolveWaiters(takeWaiters(this.navWaiters), {
            status: 'superseded',
            message: 'select',
          });
        }
        // latest-wins: prior select tickets are superseded
        if (this.pendingSelect) {
          resolveWaiters(takeWaiters(this.selectWaiters), {
            status: 'superseded',
            message: 'latest_select',
          });
        }
        this.pendingSelect = normalizeTrack(command.track);
        this.selectWaiters.push(waiter);
        if (!this.pendingClear) this.bumpInterrupt();
        break;
      case 'seek':
        // latest-wins: all seek waiters share the final seek result
        this.pendingSeek = command.seconds;
        this.seekWaiters.push(waiter);
        break;
      case 'togglePlay':
        this.pendingToggle = !this.pendingToggle;
        this.toggleWaiters.push(waiter);
        // Cancel in-flight load (toggle-while-loading) without waiting for playUrl.
        if (!this.pendingClear && this.interruptPlay) this.bumpInterrupt();
        break;
      case 'switchQuality':
        if (this.pendingQuality != null) {
          resolveWaiters(takeWaiters(this.qualityWaiters), {
            status: 'superseded',
            message: 'latest_quality',
          });
        }
        this.pendingQuality = command.quality;
        this.qualityWaiters.push(waiter);
        break;
      case 'removeTrack':
        this.pendingFmRecovery = null;
        this.pendingRemoves.push({ index: command.index, waiter });
        break;
      case 'ended':
        if (this.endedEpochHandled === this.epoch && !this.pendingEnded) {
          waiter.resolve({ status: 'noop' });
          break;
        }
        this.pendingEnded = true;
        this.endedWaiters.push(waiter);
        break;
      case 'playAll':
        if (this.pendingPlayAll) {
          resolveWaiters(takeWaiters(this.playAllWaiters), {
            status: 'superseded',
            message: 'latest_playAll',
          });
        }
        this.pendingFmRecovery = null;
        // playAll replaces pending select/nav/ended (not a pending clear barrier)
        if (this.pendingSelect) {
          this.pendingSelect = null;
          resolveWaiters(takeWaiters(this.selectWaiters), {
            status: 'superseded',
            message: 'playAll',
          });
        }
        if (this.navDelta !== 0 || this.navWaiters.length > 0) {
          this.navDelta = 0;
          resolveWaiters(takeWaiters(this.navWaiters), {
            status: 'superseded',
            message: 'playAll',
          });
        }
        if (this.pendingEnded) {
          this.pendingEnded = false;
          resolveWaiters(takeWaiters(this.endedWaiters), {
            status: 'superseded',
            message: 'playAll',
          });
        }
        this.pendingPlayAll = {
          tracks: command.tracks.map(normalizeTrack),
          startIndex: command.startIndex ?? 0,
          queueMode: command.queueMode ?? 'normal',
        };
        this.playAllWaiters.push(waiter);
        if (!this.pendingClear) this.bumpInterrupt();
        break;
      case 'addToQueue':
        this.pendingFmRecovery = null;
        this.pendingAdds.push(normalizeTrack(command.track));
        this.addWaiters.push(waiter);
        break;
      default:
        waiter.resolve({ status: 'noop' });
        break;
    }
  }

  private scheduleDrain(): Promise<void> {
    this.drainTail = this.drainTail.then(
      () => this.runDrain(),
      () => this.runDrain(),
    );
    return this.drainTail;
  }

  private async runDrain(): Promise<void> {
    while (this.hasWork()) {
      try {
        await this.drainOnce();
      } catch (e) {
        // Per-command paths should settle waiters; this keeps drainTail alive.
        console.error('playbackCommandCoordinator drainOnce', e);
      }
    }
  }

  private hasWork(): boolean {
    return (
      this.pendingClear
      || this.pendingPlayAll != null
      || this.pendingFmRecovery != null
      || this.pendingRemoves.length > 0
      || this.pendingAdds.length > 0
      || this.pendingSelect != null
      || this.navDelta !== 0
      || this.navWaiters.length > 0 // next+prev may cancel to delta 0 with waiters left
      || this.pendingSeek != null
      || this.pendingToggle
      || this.toggleWaiters.length > 0
      || this.pendingQuality != null
      || this.pendingEnded
    );
  }

  private async drainOnce(): Promise<void> {
    // 1) Barrier clear
    if (this.pendingClear) {
      this.pendingClear = false;
      const waiters = takeWaiters(this.clearWaiters);
      try {
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
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        this.epoch += 1;
        this.endedEpochHandled = this.epoch;
        resolveWaiters(waiters, failedResult(e, 'clear_failed'));
      }
      return;
    }

    // 2) playAll / replace queue
    if (this.pendingPlayAll) {
      const job = this.pendingPlayAll;
      this.pendingPlayAll = null;
      const waiters = takeWaiters(this.playAllWaiters);
      try {
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
          resolveWaiters(waiters, r);
          return;
        }
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'playAll_failed'));
      }
      return;
    }

    // 3) Adds (resolve only add tickets)
    if (this.pendingAdds.length) {
      const adds = this.pendingAdds;
      this.pendingAdds = [];
      const waiters = takeWaiters(this.addWaiters);
      try {
        const state = this.deps.getState();
        const queue = [...state.queue];
        for (const t of adds) {
          if (!queue.some((q) => q.FileHash === t.FileHash)) queue.push(t);
        }
        this.deps.patchState({ queue });
        this.deps.saveQueue();
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'add_failed'));
      }
      return;
    }

    // 4) Removes (serial, one per step — waiter bound to this index)
    if (this.pendingRemoves.length) {
      const job = this.pendingRemoves.shift()!;
      try {
        await this.applyRemove(job.index);
        job.waiter.resolve({ status: 'ok' });
      } catch (e) {
        job.waiter.resolve(failedResult(e, 'remove_failed'));
      }
      return;
    }

    // 5) Select track (latest-wins already applied in mailbox)
    if (this.pendingSelect) {
      const track = this.pendingSelect;
      this.pendingSelect = null;
      const waiters = takeWaiters(this.selectWaiters);
      this.navDelta = 0;
      resolveWaiters(takeWaiters(this.navWaiters), {
        status: 'superseded',
        message: 'select',
      });
      try {
        const r = await this.playInterruptible(track);
        if (r.status === 'ok') {
          this.epoch += 1;
          this.endedEpochHandled = -1;
        }
        resolveWaiters(waiters, r);
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'select_failed'));
      }
      return;
    }

    // 6) Relative navigation (including net-zero next+prev cancel)
    if (this.navDelta !== 0 || this.navWaiters.length > 0) {
      const delta = this.navDelta;
      this.navDelta = 0;
      const waiters = takeWaiters(this.navWaiters);
      if (delta === 0) {
        resolveWaiters(waiters, { status: 'ok' });
        return;
      }
      try {
        const r = await this.applyNav(delta);
        if (r.status === 'ok') {
          this.epoch += 1;
          this.endedEpochHandled = -1;
        }
        resolveWaiters(waiters, r);
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'nav_failed'));
      }
      return;
    }

    // 7) Seek latest — only seek waiters
    if (this.pendingSeek != null) {
      const sec = this.pendingSeek;
      this.pendingSeek = null;
      const waiters = takeWaiters(this.seekWaiters);
      try {
        await this.deps.seek(sec);
        this.deps.patchState({ currentTime: sec });
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'seek_failed'));
      }
      return;
    }

    // 8) Quality transaction — only quality waiters
    if (this.pendingQuality != null) {
      const q = this.pendingQuality;
      this.pendingQuality = null;
      const waiters = takeWaiters(this.qualityWaiters);
      const before = snapshotPlayback(this.deps.getState());
      try {
        const r = await this.deps.switchQuality(q);
        if (r && typeof r === 'object' && r.status === 'failed') {
          restorePlayback(this.deps, before);
          resolveWaiters(waiters, {
            status: 'failed',
            message: r.message || 'quality_switch_failed',
          });
          return;
        }
        if (r && typeof r === 'object' && r.status === 'superseded') {
          resolveWaiters(waiters, { status: 'superseded' });
          return;
        }
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        restorePlayback(this.deps, before);
        resolveWaiters(waiters, failedResult(e, 'quality_switch_failed'));
      }
      return;
    }

    // 9) Toggle play — only toggle waiters (even toggles net to noop)
    if (this.pendingToggle || this.toggleWaiters.length > 0) {
      const shouldFlip = this.pendingToggle;
      this.pendingToggle = false;
      const waiters = takeWaiters(this.toggleWaiters);
      if (!shouldFlip) {
        resolveWaiters(waiters, { status: 'noop' });
        return;
      }
      try {
        const state = this.deps.getState();
        if (!state.currentTrack) {
          resolveWaiters(waiters, { status: 'noop' });
          return;
        }
        if (state.isLoading) {
          const seq = this.deps.invalidatePlaybackIntent();
          await this.deps.stopInvalidatedPlayback(seq);
          this.deps.patchState({
            playbackPhase: 'paused',
            ...flagsFromPhase('paused'),
          });
          resolveWaiters(waiters, { status: 'ok' });
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
        resolveWaiters(waiters, { status: 'ok' });
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'toggle_failed'));
      }
      return;
    }

    // 10) Ended (once per epoch) — only ended waiters
    if (this.pendingEnded) {
      this.pendingEnded = false;
      const waiters = takeWaiters(this.endedWaiters);
      if (this.endedEpochHandled === this.epoch) {
        resolveWaiters(waiters, { status: 'noop' });
        return;
      }
      const epochAtStart = this.epoch;
      this.endedEpochHandled = epochAtStart;
      try {
        const r = await this.applyNav(1, /* fromEnded */ true);
        if (r.status === 'ok') {
          this.epoch = epochAtStart + 1;
          if (this.endedEpochHandled === epochAtStart) {
            this.endedEpochHandled = -1;
          }
        }
        resolveWaiters(waiters, r);
      } catch (e) {
        resolveWaiters(waiters, failedResult(e, 'ended_failed'));
      }
      return;
    }

    // 11) Timer-backed FM recovery — no external waiter is held while the
    // recommendation timer is pending; this work is enqueued only after a
    // retry has actually appended fresh tracks.
    if (this.pendingFmRecovery) {
      const recovery = this.pendingFmRecovery;
      this.pendingFmRecovery = null;
      const state = this.deps.getState();
      const sameTrack = recovery.trackKey
        ? state.currentTrack?.FileHash === recovery.trackKey
        : state.currentTrack === recovery.currentTrack;
      const stillAtOriginalTail =
        state.queue === recovery.queueRef
        && state.queueMode === 'personalFm'
        && this.epoch === recovery.epoch
        && sameTrack
        && state.currentIndex === recovery.currentIndex
        && recovery.currentIndex === recovery.queueLength - 1
        && state.queue.length === recovery.queueLength + recovery.appendedCount;
      if (!stillAtOriginalTail) return;

      const nextIndex = recovery.currentIndex + recovery.delta;
      const track = state.queue[nextIndex];
      if (!track) return;
      try {
        const r = await this.playInterruptible(track);
        if (r.status === 'ok') {
          this.epoch += 1;
          if (this.endedEpochHandled === recovery.epoch) {
            this.endedEpochHandled = -1;
          }
        }
      } catch {
        // Recovery has no waiter; a later user command can retry explicitly.
      }
      return;
    }
  }

  /**
   * Run playTrack but allow a newer select/nav/clear to interrupt the wait so the
   * orchestrator can start the new track immediately (transitionSeq supersede).
   */
  private async playInterruptible(
    track: Track,
  ): Promise<PlaybackCommandResult> {
    const playPromise = this.deps
      .playTrack(track)
      .then(
        (r) => mapPlayResult(r),
        (e) => failedResult(e, 'play_failed'),
      );
    let interrupted = false;
    const interruptPromise = new Promise<PlaybackCommandResult>((resolve) => {
      this.interruptPlay = () => {
        interrupted = true;
        resolve({ status: 'superseded' });
      };
    });
    try {
      const result = await Promise.race([playPromise, interruptPromise]);
      if (interrupted) {
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

    // Continuous recommendation session: never wrap the queue like list-loop.
    // Prefetch near the tail; block and append when the next step is past the end.
    if (mode === 'personalFm') {
      const remain = Math.max(0, state.queue.length - idx - 1);
      if (delta > 0 && this.deps.appendPersonalFm && remain <= 2) {
        // Near tail: ensure more recommendations exist before/while advancing.
        if (remain === 0 || idx + delta >= state.queue.length) {
          const recoveryBase = {
            queueRef: state.queue,
            currentTrack: state.currentTrack,
            trackKey: state.currentTrack?.FileHash || '',
            currentIndex: idx,
            queueLength: state.queue.length,
            delta,
            epoch: this.epoch,
          };
          const options: PersonalFmAppendOptions = {
            onRetrySuccess: (result) => this.queueFmRecovery(recoveryBase, result),
          };
          const ok = await this.deps.appendPersonalFm(options);
          if (!ok && idx + delta >= this.deps.getState().queue.length) {
            return { status: 'noop', message: 'fm_exhausted' };
          }
        } else {
          void this.deps.appendPersonalFm();
        }
      }

      let nextIdx = idx + delta;
      if (loop === 'random' && this.deps.getState().queue.length > 1) {
        const qNow = this.deps.getState().queue;
        let pick = Math.floor(Math.random() * qNow.length);
        if (qNow.length > 1) {
          while (pick === idx) pick = Math.floor(Math.random() * qNow.length);
        }
        nextIdx = pick;
      }

      const q = this.deps.getState().queue;
      if (!q.length || nextIdx < 0 || nextIdx >= q.length) {
        // Past end after a failed/empty append — stop, do not wrap to song 0.
        return { status: 'noop', message: 'fm_exhausted' };
      }
      return this.playInterruptible(q[nextIdx]);
    }

    let nextIdx = idx;
    if (loop === 'random' && state.queue.length > 1) {
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

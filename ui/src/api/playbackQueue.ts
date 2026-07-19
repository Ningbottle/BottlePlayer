import { normalizeTrack, type Track } from './normalizer';
import type { QualityOption } from './playbackOrchestrator';
import type { PlaybackPhase } from './playbackPhase';

export type QueueState = {
  queue: Track[];
  currentIndex: number;
  currentTrack: Track | null;
  isPlaying: boolean;
  isLoading: boolean;
  queueMode: 'normal' | 'personalFm';
  audio: HTMLAudioElement | null;
  /** Optional; when present on extended store state, residual cleanup clears it. */
  errorMsg?: string;
  currentTime?: number;
  duration?: number;
  isPreview?: boolean;
  vipRequired?: boolean;
  availableQualities?: QualityOption[];
  playbackPhase?: PlaybackPhase;
};

export type PlaybackQueueDeps = {
  getState: () => QueueState;
  saveQueue: () => void;
  /** May return PlaybackResult or void; queue commands await so the serial chain includes play. */
  playTrack: (track: Track) => void | Promise<unknown>;
  skipSession: () => void;
  invalidatePlaybackIntent: () => number;
  stopInvalidatedPlayback: (seq: number) => Promise<void>;
  hasBackend: () => boolean;
};

/**
 * @deprecated Phase 2: production playback uses PlaybackCommandCoordinator.
 * This helper remains for isolated playbackQueue unit tests only — do not add
 * a second production lock on top of the coordinator.
 */
let commandTail: Promise<unknown> = Promise.resolve();
let queueLocked = false;

/**
 * Run `fn` exclusively (test/legacy helper). Prefer coordinator.dispatch in app code.
 */
export function enqueueQueueCommand<T>(fn: () => T | Promise<T>): Promise<T> {
  const run = (async (): Promise<T> => {
    while (queueLocked) {
      await commandTail;
    }
    queueLocked = true;
    try {
      return await fn();
    } finally {
      queueLocked = false;
    }
  })();

  commandTail = run.then(
    () => undefined,
    () => undefined,
  );
  return run;
}

/** Test-only: reset serial chain between cases. */
export function __resetQueueCommandChainForTests(): void {
  commandTail = Promise.resolve();
  queueLocked = false;
}

function clearPlaybackResiduals(state: QueueState): void {
  state.isPlaying = false;
  state.isLoading = false;
  if (state.errorMsg !== undefined) {
    state.errorMsg = '';
  }
  if (state.currentTime !== undefined) state.currentTime = 0;
  if (state.duration !== undefined) state.duration = 0;
  if (state.isPreview !== undefined) state.isPreview = false;
  if (state.vipRequired !== undefined) state.vipRequired = false;
  if (state.availableQualities !== undefined) state.availableQualities = [];
  if (state.playbackPhase !== undefined) state.playbackPhase = 'idle';
}

export function playAll(
  deps: PlaybackQueueDeps,
  tracks: Track[],
  startIndex = 0,
): Promise<void> {
  return enqueueQueueCommand(async () => {
    const state = deps.getState();
    state.queue = tracks.map(normalizeTrack);
    state.currentIndex = startIndex;
    state.queueMode = 'normal';
    deps.saveQueue();
    if (state.queue.length > startIndex) {
      await deps.playTrack(state.queue[startIndex]);
    }
  });
}

export function playPersonalFm(
  deps: PlaybackQueueDeps,
  tracks: Track[],
  startIndex = 0,
): Promise<void> {
  return enqueueQueueCommand(async () => {
    const state = deps.getState();
    state.queue = tracks.map(normalizeTrack);
    state.currentIndex = startIndex;
    state.queueMode = 'personalFm';
    deps.saveQueue();
    if (state.queue.length > startIndex) {
      await deps.playTrack(state.queue[startIndex]);
    }
  });
}

export function addToQueue(deps: PlaybackQueueDeps, track: Track): Promise<void> {
  return enqueueQueueCommand(() => {
    const state = deps.getState();
    const normalized = normalizeTrack(track);
    const exists = state.queue.some((t) => t.FileHash === normalized.FileHash);
    if (!exists) {
      state.queue.push(normalized);
      deps.saveQueue();
    }
  });
}

export function removeFromQueue(deps: PlaybackQueueDeps, index: number): Promise<void> {
  return enqueueQueueCommand(async () => {
    const state = deps.getState();
    if (index < 0 || index >= state.queue.length) return;

    state.queue.splice(index, 1);

    if (state.currentIndex === index) {
      if (state.queue.length === 0) {
        // Align with clearQueue: invalidate + stop backend so audio/phase do not
        // keep playing with an empty queue.
        const stopSeq = deps.invalidatePlaybackIntent();
        deps.skipSession();
        state.currentIndex = -1;
        state.currentTrack = null;
        clearPlaybackResiduals(state);
        if (deps.hasBackend()) {
          await deps.stopInvalidatedPlayback(stopSeq);
        } else if (state.audio) {
          try {
            state.audio.pause();
            state.audio.src = '';
          } catch {
            /* ignore */
          }
        }
      } else {
        state.currentIndex = state.currentIndex % state.queue.length;
        await deps.playTrack(state.queue[state.currentIndex]);
      }
    } else if (state.currentIndex > index) {
      state.currentIndex--;
    }

    deps.saveQueue();
  });
}

export function clearQueue(deps: PlaybackQueueDeps): Promise<void> {
  return enqueueQueueCommand(async () => {
    const state = deps.getState();
    const clearSeq = deps.invalidatePlaybackIntent();
    state.queue = [];
    state.currentIndex = -1;
    state.currentTrack = null;
    clearPlaybackResiduals(state);
    if (deps.hasBackend()) {
      await deps.stopInvalidatedPlayback(clearSeq);
    } else if (state.audio) {
      try {
        state.audio.pause();
        state.audio.src = '';
      } catch {
        /* ignore */
      }
    }
    deps.saveQueue();
  });
}

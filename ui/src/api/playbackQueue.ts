import { normalizeTrack, type Track } from './normalizer';

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
 * Module-level serial entry for queue mutations.
 * When the lock is free, `fn` starts in the current turn (sync prefix runs immediately
 * so fire-and-forget playAll still assigns queue/currentIndex before the caller continues).
 * Concurrent callers wait on the promise tail and cannot interleave mid-mutation.
 */
let commandTail: Promise<unknown> = Promise.resolve();
let queueLocked = false;

/**
 * Run `fn` exclusively. Errors surface to the caller; the chain continues either way.
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
        deps.skipSession();
        state.currentIndex = -1;
        state.currentTrack = null;
        clearPlaybackResiduals(state);
        if (state.audio) {
          state.audio.src = '';
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

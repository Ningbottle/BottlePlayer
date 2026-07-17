import { normalizeTrack, type Track } from './normalizer';

export type QueueState = {
  queue: Track[];
  currentIndex: number;
  currentTrack: Track | null;
  isPlaying: boolean;
  isLoading: boolean;
  queueMode: 'normal' | 'personalFm';
  audio: HTMLAudioElement | null;
};

export type PlaybackQueueDeps = {
  getState: () => QueueState;
  saveQueue: () => void;
  /** May return PlaybackResult or void; callers always fire-and-forget. */
  playTrack: (track: Track) => void | Promise<unknown>;
  skipSession: () => void;
  invalidatePlaybackIntent: () => number;
  stopInvalidatedPlayback: (seq: number) => Promise<void>;
  hasBackend: () => boolean;
};

export function playAll(deps: PlaybackQueueDeps, tracks: Track[], startIndex = 0) {
  const state = deps.getState();
  state.queue = tracks.map(normalizeTrack);
  state.currentIndex = startIndex;
  state.queueMode = 'normal';
  deps.saveQueue();
  if (state.queue.length > startIndex) {
    void deps.playTrack(state.queue[startIndex]);
  }
}

export function playPersonalFm(deps: PlaybackQueueDeps, tracks: Track[], startIndex = 0) {
  const state = deps.getState();
  state.queue = tracks.map(normalizeTrack);
  state.currentIndex = startIndex;
  state.queueMode = 'personalFm';
  deps.saveQueue();
  if (state.queue.length > startIndex) {
    void deps.playTrack(state.queue[startIndex]);
  }
}

export function addToQueue(deps: PlaybackQueueDeps, track: Track) {
  const state = deps.getState();
  const normalized = normalizeTrack(track);
  const exists = state.queue.some((t) => t.FileHash === normalized.FileHash);
  if (!exists) {
    state.queue.push(normalized);
    deps.saveQueue();
  }
}

export function removeFromQueue(deps: PlaybackQueueDeps, index: number) {
  const state = deps.getState();
  if (index < 0 || index >= state.queue.length) return;

  state.queue.splice(index, 1);

  if (state.currentIndex === index) {
    if (state.queue.length === 0) {
      deps.skipSession();
      state.currentIndex = -1;
      state.currentTrack = null;
      if (state.audio) {
        state.audio.src = '';
        state.isPlaying = false;
        state.isLoading = false;
      }
    } else {
      state.currentIndex = state.currentIndex % state.queue.length;
      void deps.playTrack(state.queue[state.currentIndex]);
    }
  } else if (state.currentIndex > index) {
    state.currentIndex--;
  }

  deps.saveQueue();
}

export function clearQueue(deps: PlaybackQueueDeps) {
  const state = deps.getState();
  const clearSeq = deps.invalidatePlaybackIntent();
  state.queue = [];
  state.currentIndex = -1;
  state.currentTrack = null;
  state.isPlaying = false;
  state.isLoading = false;
  if (deps.hasBackend()) {
    void deps.stopInvalidatedPlayback(clearSeq);
  } else if (state.audio) {
    try {
      state.audio.pause();
      state.audio.src = '';
    } catch {
      /* ignore */
    }
  }
  deps.saveQueue();
}

import { reactive, watch } from 'vue';
import { Track, fetchCoverImage } from './normalizer';
import { Html5AudioBackend } from './html5Backend';
import type { PlayerBackend, PlaybackEvent } from './playerBackend';
import { invoke } from '@tauri-apps/api/core';
import { PlaySessionTracker, type PlayRecord } from './playSessionTracker';
import { normalizeEqBands } from './equalizerConfig';
import { recentPlayedStore } from './recentPlayedStore';
import { playbackDiagnostics } from './playbackDiagnostics';
import {
  PlaybackOrchestrator,
  type QualityOption,
} from './playbackOrchestrator';
import {
  canTransition,
  transitionPhase,
  type PlaybackPhase,
} from './playbackPhase';
import { loadNumber } from './safeStorage';
import {
  loadJSON,
  bindQueuePersistence,
  saveQueue,
} from './playerPersistence';
import { appendPersonalFmRecommendations as appendFm } from './fmSession';
import {
  playAll as playAllImpl,
  playPersonalFm as playPersonalFmImpl,
  addToQueue as addToQueueImpl,
  removeFromQueue as removeFromQueueImpl,
  clearQueue as clearQueueImpl,
} from './playbackQueue';
import { resolveTrack } from './songUrlResolver';
import { uploadPlayHistory } from './playHistory';
import { createPlayerEq } from './usePlayerEq';

export type { Track };
export { loadNumber } from './safeStorage';

export type LoopMode = 'list' | 'single' | 'random';
export type QueueMode = 'normal' | 'personalFm';

// ── Stats play session tracking (#5 #6 #7 #8 #12) ──
// Fire-and-forget: failures are silently ignored (stats are non-critical).
// See playSessionTracker.ts for the state machine + seek-immune accumulator.
function emitPlayRecord(record: PlayRecord) {
  try {
    invoke('stats_record_play', { json: JSON.stringify(record) }).catch(() => {});
  } catch {
    // 静默失败：统计记录不影响播放
  }
}

const playSession = new PlaySessionTracker(
  emitPlayRecord,
  () => playerStore.quality || '',
  () => Date.now(),
);

interface PlayerState {
  currentTrack: Track | null;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  volume: number;
  queue: Track[];
  currentIndex: number;
  loopMode: LoopMode;
  queueMode: QueueMode;
  audio: HTMLAudioElement | null;
  isLoading: boolean;
  errorMsg: string;
  isPreview: boolean;
  vipRequired: boolean;
  /** Explicit playback phase (observability; see playbackPhase.ts). */
  playbackPhase: PlaybackPhase;
  /** 当前音质等级，如 '128', '320', 'flac' 等 */
  quality: string;
  /** 当前歌曲可用的音质选项列表 */
  availableQualities: QualityOption[];
  backend: 'html5' | null;
  eqEnabled: boolean;
  eqBands: number[];
  activePreset: string;
}

type BottleMusicAudioGlobal = Window & {
  __bottlemusic_audio__?: HTMLAudioElement;
  __bottlemusic_player_cleanup__?: () => void;
};

let activeBackend: PlayerBackend | null = null;
export let eventUnsub: (() => void) | null = null;
let initListenerCleanup: (() => void) | null = null;
let endedAdvanceInFlight = false;

function audioGlobal(): BottleMusicAudioGlobal {
  return window as unknown as BottleMusicAudioGlobal;
}

function cleanupCurrentModuleForHmr() {
  initListenerCleanup?.();
  initListenerCleanup = null;
  eventUnsub?.();
  eventUnsub = null;
  activeBackend = null;
  if (playerStore.audio) playerStore.audio.volume = playerStore.volume;
  closeWebAudioEq();
}

function publishPlayerCleanup() {
  audioGlobal().__bottlemusic_player_cleanup__ = cleanupCurrentModuleForHmr;
}

/** Test-only seam: the wired playback backend. */
export function __getActiveBackend(): PlayerBackend | null {
  return activeBackend;
}

/** Test-only seam: the play-session tracker (for ordering assertions). */
export function __getPlaySession(): PlaySessionTracker {
  return playSession;
}

export const playerStore = reactive<PlayerState>({
  currentTrack: null,
  isPlaying: false,
  currentTime: 0,
  duration: 0,
  volume: loadNumber('player_volume', 0.7, 0, 1),
  queue: loadJSON<Track[]>('player_queue', []),
  currentIndex: parseInt(localStorage.getItem('player_index') || '-1', 10),
  loopMode: (localStorage.getItem('player_loop_mode') || 'list') as LoopMode,
  queueMode: (localStorage.getItem('player_queue_mode') || 'normal') as QueueMode,
  audio: null,
  isLoading: false,
  errorMsg: '',
  isPreview: false,
  vipRequired: false,
  playbackPhase: 'idle',
  quality: localStorage.getItem('player_quality') || '128',
  availableQualities: [],
  backend: null,
  eqEnabled: localStorage.getItem('player_eq_enabled') === 'true',
  eqBands: normalizeEqBands(loadJSON<unknown>('player_eq_bands', [])),
  activePreset: localStorage.getItem('player_eq_preset') || 'Flat',
});

// ── EQ leaf (usePlayerEq) — barrel re-exports keep public API stable ──
const playerEq = createPlayerEq(() => playerStore);
export const {
  eqState,
  __resetWebAudioEqForTests,
  initWebAudioEQ,
  attachWebAudioEqSource,
  disconnectWebAudioEqSource,
  setWebAudioEqVolume,
  setWebAudioEqBand,
  setWebAudioEqEnabled,
  resumeAudioContext,
  retryEq,
} = playerEq;
const { closeWebAudioEq, makeBackendEqHooks, resetRetryState } = playerEq;

// Setup audio listeners
export function initPlayer() {
  // ── 僵尸音频防护 (Zombie Audio，见 PROJECT_LOGIC §13) ──
  // Vite HMR 热重载会重新求值本模块、生成全新的 playerStore（其 audio 为 null），
  // 而上一个模块实例创建的 <audio> 仍可能在播放。HMR 时复用同一个元素，
  // 只清理旧模块监听/Worklet，避免把当前 src 卸掉导致暂停和 00:00。
  const g = audioGlobal();
  const reusableAudio = g.__bottlemusic_audio__;

  if (playerStore.audio) return;

  let audio: HTMLAudioElement;
  if (reusableAudio) {
    try {
      g.__bottlemusic_player_cleanup__?.();
    } catch { /* ignore */ }
    audio = reusableAudio;
  } else {
    if (activeBackend) {
      try {
        eventUnsub?.();
        eventUnsub = null;
        activeBackend.shutdown().catch(() => {});
      } catch { /* ignore */ }
      activeBackend = null;
    }
    audio = new Audio();
    g.__bottlemusic_audio__ = audio;
  }

  playerStore.audio = audio;
  playerStore.isPlaying = !audio.paused && !audio.ended;
  audio.volume = playerStore.volume;
  if (Number.isFinite(audio.currentTime)) {
    playerStore.currentTime = audio.currentTime;
  }
  if (Number.isFinite(audio.duration) && audio.duration > 0) {
    playerStore.duration = audio.duration;
  }

  // Event ownership (#2): the backend (initPlayerBackend → onEvent) is the
  // SOLE source of play/pause/timeupdate/ended/error events. Only duration
  // metadata listeners live here (EQ-irrelevant, and the backend doesn't
  // surface them). This eliminates the double-'ended' handler that double-
  // fetched /song/url on every natural song end.
  const onDurationChange = () => {
    if (Number.isFinite(audio.duration) && audio.duration > 0) {
      playerStore.duration = audio.duration;
    }
  };

  const onLoadedMetadata = () => {
    if (Number.isFinite(audio.duration) && audio.duration > 0) {
      playerStore.duration = audio.duration;
    }
  };

  // Resume the AudioContext on the first user-driven play (autoplay policy).
  const onPlay = () => {
    resumeAudioContext();
  };

  audio.addEventListener('durationchange', onDurationChange);
  audio.addEventListener('loadedmetadata', onLoadedMetadata);
  audio.addEventListener('play', onPlay);
  initListenerCleanup = () => {
    audio.removeEventListener('durationchange', onDurationChange);
    audio.removeEventListener('loadedmetadata', onLoadedMetadata);
    audio.removeEventListener('play', onPlay);
  };
  publishPlayerCleanup();

  // Restore previous track on init without playing
  if (playerStore.currentIndex >= 0 && playerStore.currentIndex < playerStore.queue.length) {
    playerStore.currentTrack = playerStore.queue[playerStore.currentIndex];
    if (!playerStore.duration && playerStore.currentTrack.Duration) {
      playerStore.duration = playerStore.currentTrack.Duration;
    }
  }
}

export async function initPlayerBackend() {
  if (activeBackend) return;

  // MFS native playback is disabled — topology resolution + deadlock issues.
  // Fall back to HTML5 audio which works reliably.
  // TODO(s4-fix): re-enable native after fixing BuildTopology + deadlock.
  if (!playerStore.audio) {
    console.error('No HTML5 audio element available');
    return;
  }
  initWebAudioEQ();
  activeBackend = new Html5AudioBackend(playerStore.audio, {
    ...makeBackendEqHooks(),
    getAttachTransitionSeq: () => playbackOrchestrator.getTransitionSeq(),
    isAttachTransitionCurrent: (seq) => playbackOrchestrator.isTransitionCurrent(seq),
    recordDiagnostic: (e) => playbackDiagnostics.recordEvent(e),
  });
  playerStore.backend = 'html5';

  eventUnsub = activeBackend.onEvent(handlePlaybackEvent);
}

/** Soft phase apply for backend events — never throw on racey illegal edges. */
function applyStorePhase(to: PlaybackPhase) {
  const from = playerStore.playbackPhase;
  if (from === to) return;
  if (!canTransition(from, to)) return;
  playerStore.playbackPhase = transitionPhase(from, to);
}

function handlePlaybackEvent(e: PlaybackEvent) {
  if (e.type === 'position') {
    if (typeof e.position === 'number') {
      playerStore.currentTime = e.position;
      playSession.onTimeUpdate(e.position);
    }
    playbackDiagnostics.markActivity();
    if (typeof e.duration === 'number' && Number.isFinite(e.duration) && e.duration > 0) {
      playerStore.duration = e.duration;
    }
  } else if (e.type === 'state') {
    if (e.state === 'playing') {
      playSession.onPlay();
      playbackDiagnostics.markActivity();
      playerStore.isLoading = false;
      playerStore.isPlaying = true;
      applyStorePhase('playing');
    } else if (e.state === 'paused') {
      playSession.onPause();
      playerStore.isLoading = false;
      playerStore.isPlaying = false;
      applyStorePhase('paused');
    }
    playerStore.errorMsg = '';
  } else if (e.type === 'ended') {
    // Single event owner for 'ended' (#2): finalize the completed session, then
    // either replay (single-loop) or advance. next() no longer handles single-loop.
    playSession.onEnded();
    if (playerStore.loopMode === 'single') {
      playbackOrchestrator
        .replaySameTrack()
        .catch((err) => console.error('single-loop replay failed', err));
    } else {
      advanceAfterEnded().catch((err) => console.error('auto-next failed', err));
    }
  } else if (e.type === 'error' && e.error) {
    playerStore.isLoading = false;
    playerStore.errorMsg = e.error;
    applyStorePhase('error');
  }
}

async function advanceAfterEnded() {
  if (endedAdvanceInFlight) return;
  endedAdvanceInFlight = true;
  try {
    await next();
  } finally {
    endedAdvanceInFlight = false;
  }
}

// Watch volume and queue to persist
watch(() => playerStore.volume, (newVol) => {
  localStorage.setItem('player_volume', String(newVol));
  setWebAudioEqVolume(newVol);
  if (activeBackend) {
    activeBackend.setVolume(newVol).catch(() => {});
  } else if (playerStore.audio) {
    playerStore.audio.volume = newVol;
  }
});

watch(() => playerStore.loopMode, (newMode) => {
  localStorage.setItem('player_loop_mode', newMode);
});

watch(() => playerStore.queueMode, (newMode) => {
  localStorage.setItem('player_queue_mode', newMode);
});

bindQueuePersistence(() => ({
  queue: playerStore.queue,
  currentIndex: playerStore.currentIndex,
}));

const playbackOrchestrator = new PlaybackOrchestrator({
  backend: () => activeBackend!,
  playSession,
  resolveTrack,
  fetchCover: fetchCoverImage,
  uploadPlayHistory,
  recordRecentPlayed: (track) => recentPlayedStore.recordRecentPlayed(track),
  recordDiagnostic: (e) => playbackDiagnostics.recordEvent(e),
  getState: () => playerStore,
  patchState: (patch) => Object.assign(playerStore, patch),
  saveQueue,
});

// Actions
export async function playTrack(track: Track) {
  resetRetryState();
  initPlayer();
  // Ensure the backend exists — native is disabled, so HTML5 always initializes
  // synchronously here. This keeps playTrack a single code path (no legacy
  // direct-audio branches) while remaining robust if called before App's
  // onMounted initPlayerBackend().
  if (!activeBackend) initPlayerBackend();
  return playbackOrchestrator.switchTrack(track);
}

/** 切换音质等级 */
export function setQuality(quality: string) {
  playerStore.quality = quality;
  localStorage.setItem('player_quality', quality);

  if (playerStore.currentTrack) {
    initPlayer();
    if (!activeBackend) initPlayerBackend();
    playbackOrchestrator
      .switchQuality(quality)
      .catch((e) => console.error('Quality switch failed', e));
  }
}

async function appendPersonalFmRecommendations(): Promise<boolean> {
  return appendFm({
    getState: () => playerStore,
    saveQueue,
  });
}

export async function togglePlay() {
  if (!playerStore.currentTrack) return;

  if (!activeBackend) initPlayerBackend();

  if (playerStore.isLoading) {
    await playbackOrchestrator.cancelPendingPlayback();
    return;
  }

  if (playerStore.isPlaying) {
    await activeBackend!.pause();
  } else {
    await playbackOrchestrator.resumeOrReloadCurrent();
  }
}

export async function next() {
  if (playerStore.queue.length === 0) return;

  let nextIdx = playerStore.currentIndex;
  // #11: next() always advances — single-loop replay is handled in the 'ended'
  // handler, not here. Coupling loop semantics to the transient isPlaying flag
  // made UI-next and auto-next disagree.
  if (playerStore.loopMode === 'random') {
    nextIdx = Math.floor(Math.random() * playerStore.queue.length);
  } else if (playerStore.queueMode === 'personalFm' && playerStore.currentIndex >= playerStore.queue.length - 1) {
    try {
      const appended = await appendPersonalFmRecommendations();
      if (!appended) return;
      nextIdx = playerStore.currentIndex + 1;
    } catch (e) {
      console.warn('Personal FM recommendation append failed:', e);
      return;
    }
  } else {
    nextIdx = (playerStore.currentIndex + 1) % playerStore.queue.length;
  }

  if (nextIdx >= 0 && nextIdx < playerStore.queue.length) {
    await playTrack(playerStore.queue[nextIdx]);
  }
}

export function prev() {
  if (playerStore.queue.length === 0) return;

  let prevIdx = playerStore.currentIndex;
  if (playerStore.loopMode === 'random') {
    prevIdx = Math.floor(Math.random() * playerStore.queue.length);
  } else {
    prevIdx = playerStore.currentIndex - 1;
    if (prevIdx < 0) prevIdx = playerStore.queue.length - 1;
  }

  if (prevIdx >= 0 && prevIdx < playerStore.queue.length) {
    playTrack(playerStore.queue[prevIdx]);
  }
}

export async function seek(seconds: number) {
  await activeBackend!.seek(seconds);
  playerStore.currentTime = seconds;
}

export async function setVolume(vol: number) {
  playerStore.volume = Math.max(0, Math.min(1, vol));
}

function queueDeps() {
  return {
    getState: () => playerStore,
    saveQueue,
    playTrack,
    skipSession: () => playSession.skip(),
    invalidatePlaybackIntent: () => playbackOrchestrator.invalidatePlaybackIntent(),
    stopInvalidatedPlayback: (seq: number) =>
      playbackOrchestrator.stopInvalidatedPlayback(seq),
    hasBackend: () => !!activeBackend,
  };
}

export function playAll(tracks: Track[], startIndex = 0) {
  return playAllImpl(queueDeps(), tracks, startIndex);
}

export function playPersonalFm(tracks: Track[], startIndex = 0) {
  return playPersonalFmImpl(queueDeps(), tracks, startIndex);
}

export function addToQueue(track: Track) {
  return addToQueueImpl(queueDeps(), track);
}

export function removeFromQueue(index: number) {
  return removeFromQueueImpl(queueDeps(), index);
}

/** Empty the play queue and stop the active backend when one is available. */
export function clearQueue() {
  return clearQueueImpl(queueDeps());
}

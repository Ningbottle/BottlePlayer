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
  flagsFromPhase,
  transitionPhase,
  type PlaybackPhase,
} from './playbackPhase';
import { loadNumber } from './safeStorage';
import {
  loadJSON,
  bindQueuePersistence,
  saveQueue,
  flushSaveQueue,
} from './playerPersistence';
import { appendPersonalFmRecommendations as appendFm } from './fmSession';
import { __resetQueueCommandChainForTests } from './playbackQueue';
import {
  PlaybackCommandCoordinator,
  type PlaybackCommand,
} from './playbackCommandCoordinator';
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
let playbackCoordinator: PlaybackCommandCoordinator | null = null;

function audioGlobal(): BottleMusicAudioGlobal {
  return window as unknown as BottleMusicAudioGlobal;
}

/**
 * App exit: stop media without clearing the queue (queue already flushed).
 * Do NOT use dispose() here — that barrier empties the queue and would
 * overwrite localStorage with an empty session.
 */
async function shutdownCoordinatorInstance(): Promise<void> {
  const coord = playbackCoordinator;
  playbackCoordinator = null;
  if (coord) {
    try {
      await coord.shutdown();
    } catch {
      /* shutdown best-effort */
    }
  }
}

/**
 * HMR: cancel in-flight intents without pause / src clear on the shared audio.
 * Must NOT call dispose() — that barrier empties <audio> when backend is already null.
 */
function detachCoordinatorForHmr(): void {
  const coord = playbackCoordinator;
  playbackCoordinator = null;
  if (coord) {
    void coord.detach().catch(() => {});
  }
}

function cleanupCurrentModuleForHmr() {
  initListenerCleanup?.();
  initListenerCleanup = null;
  eventUnsub?.();
  eventUnsub = null;
  // Detach first (invalidate orchestrator) while backend ref still exists for
  // any in-flight path that needs consistent deps; do not barrier-stop audio.
  detachCoordinatorForHmr();
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

/**
 * Phase is authoritative: project isPlaying / isLoading from phase.
 * Soft-ignore illegal edges (backend races) instead of throwing.
 */
function applyStorePhase(to: PlaybackPhase) {
  const from = playerStore.playbackPhase;
  if (from === to) {
    Object.assign(playerStore, flagsFromPhase(to));
    return;
  }
  if (!canTransition(from, to)) return;
  playerStore.playbackPhase = transitionPhase(from, to);
  Object.assign(playerStore, flagsFromPhase(to));
}

/** Patch store; when playbackPhase is set, flags are always derived from it. */
function patchPlayerState(patch: Partial<typeof playerStore>) {
  Object.assign(playerStore, patch);
  if (patch.playbackPhase != null) {
    Object.assign(playerStore, flagsFromPhase(patch.playbackPhase));
  }
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
      applyStorePhase('playing');
    } else if (e.state === 'paused') {
      playSession.onPause();
      applyStorePhase('paused');
    }
    playerStore.errorMsg = '';
  } else if (e.type === 'ended') {
    playSession.onEnded();
    // All ended paths go through coordinator (incl. single-loop via applyNav fromEnded).
    ensureCoordinator()
      .dispatch({ type: 'ended' })
      .catch((err) => console.error('auto-next failed', err));
  } else if (e.type === 'error' && e.error) {
    playSession.onPause();
    playerStore.errorMsg = e.error;
    applyStorePhase('error');
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
  patchState: patchPlayerState,
  saveQueue,
});

/** Core play — only coordinator (and tests) should call this, not UI. */
async function playTrackCore(track: Track) {
  resetRetryState();
  initPlayer();
  if (!activeBackend) initPlayerBackend();
  return playbackOrchestrator.switchTrack(track);
}

function ensureCoordinator(): PlaybackCommandCoordinator {
  if (!playbackCoordinator) {
    playbackCoordinator = new PlaybackCommandCoordinator({
      getState: () => playerStore as any,
      patchState: patchPlayerState,
      saveQueue,
      playTrack: async (track) => playTrackCore(track),
      switchQuality: async (quality) => {
        if (!activeBackend) initPlayerBackend();
        return playbackOrchestrator.switchQuality(quality);
      },
      seek: async (seconds) => {
        if (!activeBackend) initPlayerBackend();
        await activeBackend!.seek(seconds);
      },
      pause: async () => {
        if (activeBackend) await activeBackend.pause();
      },
      resumeOrReload: async () => {
        if (!activeBackend) initPlayerBackend();
        return playbackOrchestrator.resumeOrReloadCurrent();
      },
      invalidatePlaybackIntent: () => playbackOrchestrator.invalidatePlaybackIntent(),
      stopInvalidatedPlayback: (seq) => playbackOrchestrator.stopInvalidatedPlayback(seq),
      skipSession: () => playSession.skip(),
      hasBackend: () => !!activeBackend,
      appendPersonalFm: async () =>
        appendFm({
          getState: () => playerStore,
          saveQueue,
        }),
    });
  }
  return playbackCoordinator;
}

function readyForPlayback() {
  initPlayer();
  if (!activeBackend) initPlayerBackend();
  return ensureCoordinator();
}

// ── Public adapters (preserve exports; all intents go through coordinator) ──

export async function playTrack(track: Track) {
  return readyForPlayback().dispatch({ type: 'selectTrack', track });
}

/** 切换音质等级 — commit only on success (coordinator restores snapshot on failure). */
export async function setQuality(quality: string) {
  if (!playerStore.currentTrack) {
    playerStore.quality = quality;
    localStorage.setItem('player_quality', quality);
    return { status: 'ok' as const };
  }

  const result = await readyForPlayback().dispatch({ type: 'switchQuality', quality });
  if (result.status === 'ok') {
    playerStore.quality = quality;
    localStorage.setItem('player_quality', quality);
  } else if (result.status === 'failed' && result.message) {
    playerStore.errorMsg = result.message;
  }
  return result;
}

export async function togglePlay() {
  if (!playerStore.currentTrack) return;
  return readyForPlayback().dispatch({ type: 'togglePlay' });
}

export async function next() {
  if (playerStore.queue.length === 0) return;
  return readyForPlayback().dispatch({ type: 'next' });
}

export async function prev() {
  if (playerStore.queue.length === 0) return;
  return readyForPlayback().dispatch({ type: 'prev' });
}

export async function seek(seconds: number) {
  return readyForPlayback().dispatch({ type: 'seek', seconds });
}

export async function setVolume(vol: number) {
  playerStore.volume = Math.max(0, Math.min(1, vol));
}

export function playAll(tracks: Track[], startIndex = 0) {
  return readyForPlayback().dispatch({
    type: 'playAll',
    tracks,
    startIndex,
    queueMode: 'normal',
  });
}

export function playPersonalFm(tracks: Track[], startIndex = 0) {
  return readyForPlayback().dispatch({
    type: 'playAll',
    tracks,
    startIndex,
    queueMode: 'personalFm',
  });
}

export function addToQueue(track: Track) {
  return readyForPlayback().dispatch({ type: 'addToQueue', track });
}

export function removeFromQueue(index: number) {
  return readyForPlayback().dispatch({ type: 'removeTrack', index });
}

/** Empty the play queue and stop the active backend when one is available. */
export function clearQueue() {
  return readyForPlayback().dispatch({ type: 'clearQueue' });
}

/**
 * App exit / pagehide: persist queue first, then stop media WITHOUT clearing
 * the queue. Do not use for HMR (use module cleanup → detach).
 */
export async function disposePlayerRuntime(): Promise<void> {
  // Critical: flush current queue before any stop so we never persist [].
  try {
    flushSaveQueue();
  } catch {
    /* ignore */
  }
  // Keep activeBackend until after stop so backend.stop is available.
  await shutdownCoordinatorInstance();
  initListenerCleanup?.();
  initListenerCleanup = null;
  eventUnsub?.();
  eventUnsub = null;
  if (activeBackend) {
    try {
      await activeBackend.shutdown();
    } catch {
      /* ignore */
    }
    activeBackend = null;
  }
  closeWebAudioEq();
}

/** Test-only: reset coordinator between tests. */
export function __resetPlaybackCoordinatorForTests() {
  playbackCoordinator = null;
  __resetQueueCommandChainForTests();
}

export type { PlaybackCommand };

// Best-effort shutdown when the shell unloads (cannot await beforeunload).
if (typeof window !== 'undefined') {
  window.addEventListener('pagehide', () => {
    void disposePlayerRuntime();
  });
}

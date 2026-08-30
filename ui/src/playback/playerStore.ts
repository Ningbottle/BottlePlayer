import { reactive, watch } from 'vue';
import { Track, fetchCoverImage } from '../api/normalizer';
import { Html5AudioBackend } from '../api/html5Backend';
import type { PlayerBackend, PlaybackEvent } from '../api/playerBackend';
import {
  getMediaRuntime,
  getOrCreateMediaRuntime,
  type MediaRuntime,
  type MediaRuntimeDeps,
} from '../api/mediaRuntime';
import { PlaySessionTracker } from './playSessionTracker';
import { recordPlay } from '../api/playStatsGateway';
import { normalizeEqBands } from '../api/equalizerConfig';
import { recentPlayedStore } from '../api/recentPlayedStore';
import { playbackDiagnostics } from './playbackDiagnostics';
import {
  PlaybackOrchestrator,
  type QualityOption,
} from '../api/playbackOrchestrator';
import {
  canTransition,
  flagsFromPhase,
  transitionPhase,
  type PlaybackPhase,
} from './playbackPhase';
import { safeGetItem, safeSetItem } from '../platform/storage/safeStorage';
import {
  loadJSON,
  loadPlayerVolume,
  savePlayerVolume,
  bindQueuePersistence,
  saveQueue,
  flushSaveQueue,
  loadQueueSnapshot,
} from '../api/playerPersistence';
import { appendPersonalFmRecommendations as appendFm, disposeFmSession } from '../api/fmSession';
import { __resetQueueCommandChainForTests } from './playbackQueue';
import {
  PlaybackCommandCoordinator,
  type PlaybackCommand,
} from '../api/playbackCommandCoordinator';
import { resolveTrack } from '../api/songUrlResolver';
import { uploadPlayHistory } from '../api/playHistory';
import { createPlayerEq } from '../api/usePlayerEq';
import { disposeAudioLevelMonitor } from '../api/audioLevelMonitor';

export type { Track };

export type LoopMode = 'list' | 'single' | 'random';
export type QueueMode = 'normal' | 'personalFm';

function parseLoopMode(value: string | null): LoopMode {
  if (value === 'single' || value === 'random') return value;
  return 'list';
}

function parseQueueMode(value: string | null): QueueMode {
  return value === 'personalFm' ? value : 'normal';
}

// ── Stats play session tracking (#5 #6 #7 #8 #12) ──
// Records flow through the play-stats gateway (its sole production owner).
// See playSessionTracker.ts for the state machine + seek-immune accumulator.
const playSession = new PlaySessionTracker(
  recordPlay,
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
  /** UI projection: a MediaRuntime-owned <audio> is available (see initPlayer). */
  hasAudio: boolean;
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

/**
 * This module generation's MediaRuntime binding. The <audio> element and the
 * PlayerBackend instance are owned by MediaRuntime (mediaRuntime.ts); the
 * store only holds this client-side reference and feeds it callbacks/commands.
 */
let moduleRuntime: MediaRuntime | null = null;
let playbackCoordinator: PlaybackCommandCoordinator | null = null;

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
  // HMR: synchronously flush any pending debounced queue save so the new
  // module reads the latest queue from localStorage. Without this, a 500ms
  // saveQueue debounce window during a track switch would leave localStorage
  // stale — the new module would restore the OLD queue while the reused
  // <audio> keeps playing the NEW track (UI/audio desync).
  //
  // flushSaveQueue is guaranteed non-throwing (best-effort persistence with
  // internal try/catch), so teardown below always runs even if localStorage
  // is over quota / unavailable.
  flushSaveQueue();
  // Detach first (invalidate orchestrator) while backend ref still exists for
  // any in-flight path that needs consistent deps; do not barrier-stop audio.
  detachCoordinatorForHmr();
  // Cancel any pending FM retry timer and invalidate the in-flight FM fetch so
  // neither can append to / save a queue that belongs to a dying module.
  disposeFmSession();
  // Media-side resources (audio listeners, backend event subscription, backend
  // ref) belong to MediaRuntime and are torn down by its detachForHmr() —
  // never duplicated here. Restore the element volume the EQ graph was holding.
  const audio = getMediaRuntime()?.audio;
  if (audio) audio.volume = playerStore.volume;
  closeWebAudioEq();
  // R3: dispose the analyser AudioContext so HMR cycles don't leak module-level
  // singletons. Dev-only — the application shutdown command does NOT call this
  // (preserves the no-blip behavior; the analyser is analysis-only, never to
  // destination).
  disposeAudioLevelMonitor();
}

/** Test-only seam: the wired playback backend. */
export function __getActiveBackend(): PlayerBackend | null {
  return moduleRuntime?.getBackend() ?? null;
}

/** Test-only seam: the play-session tracker (for ordering assertions). */
export function __getPlaySession(): PlaySessionTracker {
  return playSession;
}

// Read the persisted queue snapshot once at module eval. Atomic single-key
// read (queue + currentIndex together) — see loadQueueSnapshot in
// playerPersistence.ts. Legacy split keys are migrated transparently.
const initialQueueSnapshot = loadQueueSnapshot();

export const playerStore = reactive<PlayerState>({
  currentTrack: null,
  isPlaying: false,
  currentTime: 0,
  duration: 0,
  volume: loadPlayerVolume(),
  queue: initialQueueSnapshot.queue,
  currentIndex: initialQueueSnapshot.currentIndex,
  loopMode: parseLoopMode(safeGetItem('player_loop_mode')),
  queueMode: parseQueueMode(safeGetItem('player_queue_mode')),
  hasAudio: false,
  isLoading: false,
  errorMsg: '',
  isPreview: false,
  vipRequired: false,
  playbackPhase: 'idle',
  quality: safeGetItem('player_quality') || '128',
  availableQualities: [],
  backend: null,
  eqEnabled: safeGetItem('player_eq_enabled') === 'true',
  eqBands: normalizeEqBands(loadJSON<unknown>('player_eq_bands', [])),
  activePreset: safeGetItem('player_eq_preset') || 'Flat',
});

// ── EQ leaf (usePlayerEq) — barrel re-exports keep public API stable ──
const playerEq = createPlayerEq({
  getAudio: () => moduleRuntime?.audio ?? null,
  getVolume: () => playerStore.volume,
  getEqEnabled: () => playerStore.eqEnabled,
  getEqBands: () => playerStore.eqBands,
});
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

/** MediaRuntimeDeps: the only channel through which the Runtime reaches Store state. */
function buildMediaRuntimeDeps(): MediaRuntimeDeps {
  return {
    initialVolume: () => playerStore.volume,
    // initialVolume comes from deps.initialVolume() — the Store preference at
    // runtime-creation time — making the Backend persistence-free.
    createBackend: (audio, initialVolume) =>
      new Html5AudioBackend(audio, {
        initialVolume,
        ...makeBackendEqHooks(),
        getAttachTransitionSeq: () => playbackOrchestrator.getTransitionSeq(),
        isAttachTransitionCurrent: (seq) => playbackOrchestrator.isTransitionCurrent(seq),
        recordDiagnostic: (e) => playbackDiagnostics.recordEvent(e),
      }),
    onBackendEvent: handlePlaybackEvent,
    onDuration: (duration) => {
      if (Number.isFinite(duration) && duration > 0) {
        playerStore.duration = duration;
      }
    },
    // Resume the AudioContext on the first user-driven play (autoplay policy).
    onFirstPlay: () => {
      resumeAudioContext();
    },
    beforeHmrDetach: cleanupCurrentModuleForHmr,
  };
}

// Setup the media runtime (audio element + listeners) and page bindings
export function initPlayer() {
  // ── 僵尸音频防护 (Zombie Audio，见 PROJECT_LOGIC §13) ──
  // Vite HMR 热重载会重新求值本模块、生成全新的 playerStore（其 runtime 为 null），
  // 而上一个模块实例的 MediaRuntime 仍持有可能正在播放的 <audio>。HMR 时通过
  // getOrCreateMediaRuntime 复用同一个元素，只清理旧模块监听/Worklet，避免把
  // 当前 src 卸掉导致暂停和 00:00。
  if (moduleRuntime && getMediaRuntime() === moduleRuntime) return;

  const existing = getMediaRuntime();
  if (existing) {
    // Reuse path: getOrCreateMediaRuntime runs the previous generation's
    // captured beforeHmrDetach (queue flush, coordinator detach, FM/EQ/
    // analyser dispose) exactly once, drops the old backend ref WITHOUT
    // pausing or clearing the element, and rebinds the SAME <audio>.
    moduleRuntime = getOrCreateMediaRuntime(buildMediaRuntimeDeps());
    // HMR queue resync: the old module's cleanup just flushed its in-memory
    // queue to localStorage (flushSaveQueue in cleanupCurrentModuleForHmr).
    // But THIS module's playerStore was created at module-eval time from
    // STALE localStorage (the 500ms saveQueue debounce hadn't fired yet).
    // Re-read now so queue/currentIndex match the actual playing track,
    // not the pre-switch snapshot that was on disk when this module loaded.
    const snapshot = loadQueueSnapshot();
    playerStore.queue = snapshot.queue;
    playerStore.currentIndex = snapshot.currentIndex;
  } else {
    if (moduleRuntime) {
      // This generation's runtime lost global ownership (fresh re-init after
      // the global owner was reset): retire its backend side before a new
      // element is created, so no zombie backend outlives the slot.
      void moduleRuntime.shutdown('shutdown');
    }
    moduleRuntime = getOrCreateMediaRuntime(buildMediaRuntimeDeps());
  }
  playerStore.hasAudio = true;
  const audio = moduleRuntime.audio;
  // R2: phase is the single source of truth. Derive isPlaying/isLoading from
  // playbackPhase (via applyStorePhase) instead of writing isPlaying directly.
  // On HMR reuse, the audio element's paused/ended state reflects the live
  // playback state — project it into phase so flags stay consistent.
  //
  // ORDER MATTERS: restore currentTrack from the persisted queue BEFORE
  // projecting phase. If phase projection happens first, currentTrack is still
  // null (it's derived from queue[currentIndex] below), so a paused audio with
  // a valid persisted queue would fall to the "no track" branch and leave
  // phase=idle — creating a "currentTrack non-null but phase=idle" inconsistency.
  // Restoring currentTrack first lets the paused-with-track branch fire.
  if (playerStore.currentIndex >= 0 && playerStore.currentIndex < playerStore.queue.length) {
    playerStore.currentTrack = playerStore.queue[playerStore.currentIndex];
    if (!playerStore.duration && playerStore.currentTrack.Duration) {
      playerStore.duration = playerStore.currentTrack.Duration;
    }
  }
  const audioIsMidPlay = !audio.paused && !audio.ended;
  if (audioIsMidPlay) {
    applyStorePhase('playing');
  } else if (playerStore.currentTrack) {
    // Paused with a restored track → paused. No track → leave idle (no source).
    applyStorePhase('paused');
  } else {
    playerStore.isPlaying = false;
    playerStore.isLoading = false;
  }
  audio.volume = playerStore.volume;
  if (Number.isFinite(audio.currentTime)) {
    playerStore.currentTime = audio.currentTime;
  }
  if (Number.isFinite(audio.duration) && audio.duration > 0) {
    playerStore.duration = audio.duration;
  }

  // Event ownership (#2): the backend (ensureBackend → onEvent) is the SOLE
  // source of play/pause/timeupdate/ended/error events. The duration metadata
  // listeners and the first-play AudioContext resume now live in MediaRuntime
  // via deps.onDuration/deps.onFirstPlay.
  // This eliminates the double-'ended' handler that double-fetched /song/url
  // on every natural song end.

  // Bind the persistence snapshot to THIS module's playerStore. Done in
  // initPlayer (not at module top level) so a re-evaluated orphan module (HMR /
  // vi.resetModules) that never calls initPlayer cannot steal the global
  // getSnapshot pointer - only the live module that owns the <audio> does.
  bindQueuePersistence(() => ({
    queue: playerStore.queue,
    currentIndex: playerStore.currentIndex,
  }));

  // NOTE: currentTrack restoration was moved ABOVE the phase projection block
  // (near the top of this function) so that phase=paused fires correctly when
  // a paused audio has a persisted queue. See R2 "ORDER MATTERS" comment above.
}

export async function initPlayerBackend() {
  if (!moduleRuntime) {
    console.error('No HTML5 audio element available');
    return;
  }
  if (moduleRuntime.getBackend()) return;

  // MFS native playback is disabled — topology resolution + deadlock issues.
  // Fall back to HTML5 audio which works reliably.
  // TODO(s4-fix): re-enable native after fixing BuildTopology + deadlock.
  initWebAudioEQ();
  // Synchronous by contract: callers read the backend on the same call stack.
  moduleRuntime.ensureBackend();
  playerStore.backend = 'html5';
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

/**
 * Patch store. isPlaying/isLoading are NEVER accepted from a patch — they are
 * ALWAYS derived from playbackPhase (R1: phase is the single source of truth).
 *
 * - Patch WITH playbackPhase: strip any caller-supplied flags, derive from phase.
 * - Patch WITHOUT playbackPhase: strip any caller-supplied flags, leave existing
 *   flag values untouched (they should already be consistent from a prior phase
 *   write). Non-flag fields (currentTime, duration, errorMsg, etc.) pass through.
 *
 * This closes the "bare flag write" hole: a caller passing `{ isPlaying: true }`
 * without a phase cannot flip the flag — the patch funnel drops it.
 */
function patchPlayerState(patch: Partial<typeof playerStore>) {
  const { isPlaying: _dropPlay, isLoading: _dropLoad, ...rest } = patch;
  void _dropPlay; void _dropLoad;
  if (rest.playbackPhase != null) {
    Object.assign(playerStore, rest, flagsFromPhase(rest.playbackPhase));
  } else {
    Object.assign(playerStore, rest);
  }
}

/** Test-only: expose patchPlayerState for phase-projection invariant tests. */
export function __patchPlayerStateForTests(patch: Partial<typeof playerStore>) {
  patchPlayerState(patch);
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
  savePlayerVolume(newVol);
  setWebAudioEqVolume(newVol);
  const backend = moduleRuntime?.getBackend();
  if (backend) {
    backend.setVolume(newVol).catch(() => {});
  } else if (moduleRuntime) {
    moduleRuntime.audio.volume = newVol;
  }
});

watch(() => playerStore.loopMode, (newMode) => {
  safeSetItem('player_loop_mode', newMode);
});

watch(() => playerStore.queueMode, (newMode) => {
  safeSetItem('player_queue_mode', newMode);
});

const playbackOrchestrator = new PlaybackOrchestrator({
  backend: () => moduleRuntime!.getBackend()!,
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
  if (!moduleRuntime?.getBackend()) initPlayerBackend();
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
        if (!moduleRuntime?.getBackend()) initPlayerBackend();
        return playbackOrchestrator.switchQuality(quality);
      },
      seek: async (seconds) => {
        if (!moduleRuntime?.getBackend()) initPlayerBackend();
        await moduleRuntime!.getBackend()!.seek(seconds);
      },
      pause: async () => {
        const backend = moduleRuntime?.getBackend();
        if (backend) await backend.pause();
      },
      resumeOrReload: async () => {
        if (!moduleRuntime?.getBackend()) initPlayerBackend();
        return playbackOrchestrator.resumeOrReloadCurrent();
      },
      invalidatePlaybackIntent: () => playbackOrchestrator.invalidatePlaybackIntent(),
      detachPlaybackIntent: () => playbackOrchestrator.detachPlaybackIntent(),
      stopInvalidatedPlayback: (seq) => playbackOrchestrator.stopInvalidatedPlayback(seq),
      skipSession: () => playSession.skip(),
      hasBackend: () => !!moduleRuntime?.getBackend(),
      // Physical no-backend fallback stays behind the MediaRuntime boundary.
      stopAndClearMedia: () => moduleRuntime?.stopAndClearMedia(),
      appendPersonalFm: async (options) =>
        appendFm({
          getState: () => playerStore,
          saveQueue,
        }, options),
    });
  }
  return playbackCoordinator;
}

function readyForPlayback() {
  initPlayer();
  if (!moduleRuntime?.getBackend()) initPlayerBackend();
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
    safeSetItem('player_quality', quality);
    return { status: 'ok' as const };
  }

  const result = await readyForPlayback().dispatch({ type: 'switchQuality', quality });
  if (result.status === 'ok') {
    playerStore.quality = quality;
    safeSetItem('player_quality', quality);
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
 * Application shutdown command: persist queue first, then stop media WITHOUT
 * clearing the queue. Installed by the composition root (main.ts) as the
 * page-unload shutdown callback via app/lifecycle/pageLifecycle.ts; do not
 * use for HMR (use module cleanup → detach).
 *
 * Shutdown orchestration stays here (Store/composition side): flush queue →
 * dispose FM → shutdown Coordinator → MediaRuntime.shutdown() → close EQ.
 */
export async function disposePlayerRuntime(): Promise<void> {
  // Critical: flush current queue before any stop so we never persist [].
  // flushSaveQueue is non-throwing (best-effort).
  flushSaveQueue();
  // Cancel FM retry timer / in-flight fetch so it cannot append after unload.
  disposeFmSession();
  // Keep the backend reachable until after coordinator stop (backend.stop is
  // part of the stop barrier), then let the runtime retire it.
  await shutdownCoordinatorInstance();
  if (moduleRuntime) {
    await moduleRuntime.shutdown('shutdown');
  }
  closeWebAudioEq();
}

/** Test-only: reset coordinator between tests. */
export function __resetPlaybackCoordinatorForTests() {
  playbackCoordinator = null;
  __resetQueueCommandChainForTests();
}

export type { PlaybackCommand };

import { reactive, watch } from 'vue';
import { apiGet } from './backend';
import { Track, normalizeTrack, fetchCoverImage } from './normalizer';
import { userStore } from './userStore';
import { Html5AudioBackend } from './html5Backend';
import type { PlayerBackend, PlaybackEvent } from './playerBackend';
import { invoke } from '@tauri-apps/api/core';
import { PlaySessionTracker, type PlayRecord } from './playSessionTracker';
import { WebAudioEq } from './webAudioEq';
import { normalizeEqBands } from './equalizerConfig';
import { prepareAudioSourceUrl } from './audioProxy';
import {
  PlaybackOrchestrator,
  type QualityOption,
  type ResolveTrackResult,
} from './playbackOrchestrator';

export type { Track };

export type LoopMode = 'list' | 'single' | 'random';

/** 上传播放历史到酷狗服务器（静默失败，不影响播放） */
async function uploadPlayHistory(track: Track) {
  try {
    if (!userStore.isLoggedIn) return; // 未登录不上传
    const mxid = track.AlbumAudioID || track.MixSongID;
    if (!mxid) return;
    const numMxid = Number(mxid);
    if (!Number.isFinite(numMxid) || numMxid <= 0) return;
    await apiGet('/playhistory/upload', {
      mxid: numMxid,
      time: Math.floor(Date.now() / 1000),
      pc: 1
    });
  } catch (e) {
    // 静默失败：播放历史上传不是关键路径，网络错误不应打断用户体验
    console.warn('播放历史上传失败（可忽略）:', e);
  }
}

// ── Safe localStorage JSON parse (#14) ──
// Module-level JSON.parse of localStorage used to throw on corrupt data and
// blank-screen the app at import time. Swallow and fall back to defaults.
function loadJSON<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key);
    if (raw == null) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

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

// ── Web Audio API EQ chain (#1 #4 #9 #10) ──
// Routes the <audio> element through a BiquadFilter chain so EQ works for the
// HTML5 backend. See webAudioEq.ts for graph-build-order / CORS / close logic.
//
// CORS note (#1): KuGou's media CDN sends no Access-Control-Allow-Origin
// (verified 2026-06-25). createMediaElementSource on a non-CORS cross-origin
// source taints (silent PCM), and setting crossOrigin='anonymous' on the raw
// CDN URL makes the load fail entirely. HTML5 playback therefore first asks
// the Tauri local audio proxy for a CORS-safe 127.0.0.1 URL. Only proxy-backed
// media is routed through WebAudio; direct fallback remains EQ-unavailable.
const webAudioEq = new WebAudioEq(() => {
  const Ctx = window.AudioContext
    || (window as unknown as { webkitAudioContext?: typeof AudioContext }).webkitAudioContext;
  return Ctx ? new Ctx() : null;
});

interface PlayerState {
  currentTrack: Track | null;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  volume: number;
  queue: Track[];
  currentIndex: number;
  loopMode: LoopMode;
  audio: HTMLAudioElement | null;
  isLoading: boolean;
  errorMsg: string;
  isPreview: boolean;
  vipRequired: boolean;
  /** 当前音质等级，如 '128', '320', 'flac' 等 */
  quality: string;
  /** 当前歌曲可用的音质选项列表 */
  availableQualities: QualityOption[];
  backend: 'html5' | 'native' | null;
  eqEnabled: boolean;
  eqBands: number[];
  activePreset: string;
}

let activeBackend: PlayerBackend | null = null;
export let eventUnsub: (() => void) | null = null;

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
  volume: parseFloat(localStorage.getItem('player_volume') || '0.7'),
  queue: loadJSON<Track[]>('player_queue', []),
  currentIndex: parseInt(localStorage.getItem('player_index') || '-1', 10),
  loopMode: (localStorage.getItem('player_loop_mode') || 'list') as LoopMode,
  audio: null,
  isLoading: false,
  errorMsg: '',
  isPreview: false,
  vipRequired: false,
  quality: localStorage.getItem('player_quality') || '128',
  availableQualities: [],
  backend: null,
  eqEnabled: localStorage.getItem('player_eq_enabled') === 'true',
  eqBands: normalizeEqBands(loadJSON<unknown>('player_eq_bands', [])),
  activePreset: localStorage.getItem('player_eq_preset') || 'Flat',
});

// ── EQ public API (delegates to webAudioEq) ──
/** Whether the EQ graph is actually active (rerouted through Web Audio API).
 *  False when the source is cross-origin non-CORS (KuGou CDN) — sliders do
 *  nothing in that state. Exposed for the UI to show a degradation notice. */
export const eqState = reactive({
  available: false,
  reason: '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。',
});

// #16: once a WebAudio element-wedge has triggered a swap, EQ is disabled for
// the rest of the session. The fresh <audio> element never calls
// createMediaElementSource, so a second wedge (and an infinite swap loop) is
// impossible. The user can retry EQ by reloading the page.
let eqDisabledForSession = false;

/** Test-only seam: reset the session-disable flag between tests. */
export function __resetEqDisabledForSession() {
  eqDisabledForSession = false;
}

export function initWebAudioEQ(audio: HTMLAudioElement, crossOriginSafe = false) {
  // #16: after a wedge-triggered element swap, never rebuild the WebAudio
  // graph — the user's environment already wedged once and may wedge again.
  // The fresh element plays directly (EQ off for the session).
  const safe = crossOriginSafe && !eqDisabledForSession;
  webAudioEq.init(audio, {
    enabled: playerStore.eqEnabled,
    bands: playerStore.eqBands,
    crossOriginSafe: safe,
    onSuspendedFail: () => {
      // #10 #16: suspended context we can't resume. On song 1 this just
      // degrades EQ (the element isn't bound yet, audio may still play via
      // the element directly if the graph wasn't built). On a SUBSEQUENT
      // track the element IS already bound to the WebAudio graph
      // (createMediaElementSource is irreversible), so a suspended context
      // means the audio is silently trapped — swap in a fresh element to
      // recover playback. eqDisabledForSession prevents infinite loops.
      eqState.available = false;
      eqState.reason = 'AudioContext 未能恢复，EQ 暂不可用。';
      if (webAudioEq.isRerouted && !eqDisabledForSession) {
        console.warn('Web Audio EQ resume failed on a routed element; swapping audio element and disabling EQ for this session.');
        swapAudioElementAfterWedge();
      }
    },
    onElementWedged: () => {
      // #16: createMediaElementSource threw InvalidStateError — the <audio>
      // is irreversibly bound to a previous source node. Swap in a fresh
      // element so playback can continue (EQ off for the session).
      console.warn('Web Audio EQ element binding failed; swapping audio element and disabling EQ for this session.');
      swapAudioElementAfterWedge();
    },
  });
  eqState.available = webAudioEq.isRerouted;
  eqState.reason = eqState.available ? '' : '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';
}

export function setWebAudioEqBand(index: number, gainDb: number) {
  webAudioEq.setBand(index, gainDb, playerStore.eqEnabled);
}

export function setWebAudioEqEnabled(enabled: boolean) {
  webAudioEq.setEnabled(enabled, playerStore.eqBands);
}

/** Resume the AudioContext after a user gesture (autoplay policy). */
export function resumeAudioContext() {
  webAudioEq.resume().catch(() => {});
}

// Setup audio listeners
export function initPlayer() {
  // ── 僵尸音频防护 (Zombie Audio，见 PROJECT_LOGIC §13) ──
  // Vite HMR 热重载会重新求值本模块、生成全新的 playerStore（其 audio 为 null），
  // 而上一个模块实例创建的 <audio> 仍在后台播放 → 多个实例重音、新代码 pause 不掉。
  // 把元素挂到 window 上：每次重载先把上一个彻底销毁，再建新的，保证全局只有一个。
  const g = window as unknown as { __bottlemusic_audio__?: HTMLAudioElement };

  // #9 #15: tear down the previous backend/EQ/context before rebuilding, so
  // HMR doesn't leak AudioContexts (browser cap ~6) or orphan event listeners.
  // #16: ONLY run this teardown on a genuine HMR reload — i.e. when the module
  // was re-evaluated (playerStore.audio is null) but a previous instance left
  // a zombie <audio> on window. Without the `!playerStore.audio` guard this
  // would fire on EVERY playTrack() call (since __bottlemusic_audio__ is set
  // after the first play), closing the WebAudio context and nulling
  // webAudioEq.ctx — which defeats init()'s `if (this.ctx) return` guard,
  // causing the next init() to call createMediaElementSource again on the
  // already-bound element → InvalidStateError → silent playback wedge.
  if (g.__bottlemusic_audio__ && !playerStore.audio) {
    try {
      eventUnsub?.();
      eventUnsub = null;
      activeBackend?.shutdown().catch(() => {});
      activeBackend = null;
      webAudioEq.close();
      const old = g.__bottlemusic_audio__;
      old.pause();
      old.removeAttribute('src');
      old.load();
    } catch { /* ignore */ }
  }

  if (playerStore.audio) return;

  if (activeBackend) {
    try {
      eventUnsub?.();
      eventUnsub = null;
      activeBackend.shutdown().catch(() => {});
    } catch { /* ignore */ }
    activeBackend = null;
  }

  const audio = new Audio();
  g.__bottlemusic_audio__ = audio;
  playerStore.audio = audio;
  audio.volume = playerStore.volume;

  // Event ownership (#2): the backend (initPlayerBackend → onEvent) is the
  // SOLE source of play/pause/timeupdate/ended/error events. Only duration
  // metadata listeners live here (EQ-irrelevant, and the backend doesn't
  // surface them). This eliminates the double-'ended' handler that double-
  // fetched /song/url on every natural song end.
  audio.addEventListener('durationchange', () => {
    if (audio.duration) {
      playerStore.duration = audio.duration;
    }
  });

  audio.addEventListener('loadedmetadata', () => {
    if (audio.duration) {
      playerStore.duration = audio.duration;
    }
  });

  // Resume the AudioContext on the first user-driven play (autoplay policy).
  audio.addEventListener('play', () => {
    resumeAudioContext();
  });

  // Restore previous track on init without playing
  if (playerStore.currentIndex >= 0 && playerStore.currentIndex < playerStore.queue.length) {
    playerStore.currentTrack = playerStore.queue[playerStore.currentIndex];
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
  activeBackend = new Html5AudioBackend(playerStore.audio, {
    prepareSourceUrl: prepareAudioSourceUrl,
    initEq: initWebAudioEQ,
  });
  playerStore.backend = 'html5';

  eventUnsub = activeBackend.onEvent(handlePlaybackEvent);
}

// #16: re-entrancy guard for swapAudioElementAfterWedge. The swap re-triggers
// playback via switchTrack, which (in pathological cases) could re-wedge and
// re-fire onElementWedged synchronously. eqDisabledForSession already prevents
// the re-wedge (the fresh element skips graph building), but this flag is a
// belt-and-suspenders guard against any swap-during-swap recursion.
let swapInProgress = false;

/**
 * #16: Recover from an irreversible WebAudio element-wedge by replacing the
 * <audio> element entirely. Called from initWebAudioEQ's `onElementWedged`
 * callback when `createMediaElementSource` throws InvalidStateError (the
 * element is already bound to a previous source node and can never be used
 * for EQ again).
 *
 * The swap:
 *   1. Sets `eqDisabledForSession = true` so the fresh element never calls
 *      createMediaElementSource (EQ off for the rest of the session). This
 *      is also the infinite-loop guard: a second wedge is impossible.
 *   2. Tears down the current backend, EQ context, and old <audio> element.
 *   3. Creates a fresh `new Audio()` with the same metadata/play listeners.
 *   4. Re-inits the Html5AudioBackend on the fresh element.
 *   5. Re-triggers playback of the current track so the user's selected song
 *      actually plays through the fresh (un-bound) element.
 */
function swapAudioElementAfterWedge() {
  if (swapInProgress) {
    return;
  }
  if (eqDisabledForSession) {
    // Already swapped once this session; EQ is off. A second wedge should be
    // impossible (fresh element skips graph building), but defensively no-op.
    return;
  }
  swapInProgress = true;
  eqDisabledForSession = true;

  const g = window as unknown as { __bottlemusic_audio__?: HTMLAudioElement };

  // 1. Tear down the old backend, EQ, and element (mirror initPlayer's HMR
  //    zombie-audio teardown path).
  try {
    eventUnsub?.();
    eventUnsub = null;
    activeBackend?.shutdown().catch(() => {});
    activeBackend = null;
    webAudioEq.close();
    const old = playerStore.audio ?? g.__bottlemusic_audio__;
    if (old) {
      old.pause();
      old.removeAttribute('src');
      old.load();
    }
  } catch { /* ignore */ }

  // 2. Create a fresh <audio> element (un-bound to any MediaElementSourceNode).
  const audio = new Audio();
  g.__bottlemusic_audio__ = audio;
  playerStore.audio = audio;
  audio.volume = playerStore.volume;

  // 3. Re-attach the metadata + play listeners (same as initPlayer).
  audio.addEventListener('durationchange', () => {
    if (audio.duration) {
      playerStore.duration = audio.duration;
    }
  });
  audio.addEventListener('loadedmetadata', () => {
    if (audio.duration) {
      playerStore.duration = audio.duration;
    }
  });
  audio.addEventListener('play', () => {
    resumeAudioContext();
  });

  // 4. Re-init the backend on the fresh element. initPlayerBackend() builds a
  //    new Html5AudioBackend; its initEq callback will call initWebAudioEQ,
  //    which now forces crossOriginSafe=false (eqDisabledForSession=true) →
  //    webAudioEq.init skips graph building → fresh element plays directly.
  initPlayerBackend();

  // 5. Surface the degradation in the UI.
  eqState.available = false;
  eqState.reason = 'EQ 已因音频元素冲突降级，刷新页面可重试。';

  swapInProgress = false;

  // 6. Re-trigger playback of the current track on the fresh element. The
  //    orchestrator's switchTrack will resolve the URL and call playUrl on
  //    the new backend. Fire-and-forget; failures are logged by the orchestrator.
  const current = playerStore.currentTrack;
  if (current) {
    playbackOrchestrator
      .switchTrack(current)
      .catch((err) => console.error('Playback recovery after EQ downgrade failed:', err));
  }
}

function handlePlaybackEvent(e: PlaybackEvent) {
  if (e.type === 'position') {
    if (typeof e.position === 'number') {
      playerStore.currentTime = e.position;
      playSession.onTimeUpdate(e.position);
    }
    if (typeof e.duration === 'number') playerStore.duration = e.duration;
  } else if (e.type === 'state') {
    if (e.state === 'playing') {
      playSession.onPlay();
      playerStore.isLoading = false;
      playerStore.isPlaying = true;
    } else if (e.state === 'paused') {
      playSession.onPause();
      playerStore.isLoading = false;
      playerStore.isPlaying = false;
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
      next();
    }
  } else if (e.type === 'error' && e.error) {
    playerStore.isLoading = false;
    playerStore.errorMsg = e.error;
  }
}

// Watch volume and queue to persist
watch(() => playerStore.volume, (newVol) => {
  localStorage.setItem('player_volume', String(newVol));
  if (activeBackend) {
    activeBackend.setVolume(newVol).catch(() => {});
  } else if (playerStore.audio) {
    playerStore.audio.volume = newVol;
  }
});

watch(() => playerStore.loopMode, (newMode) => {
  localStorage.setItem('player_loop_mode', newMode);
});

function saveQueue() {
  localStorage.setItem('player_queue', JSON.stringify(playerStore.queue));
  localStorage.setItem('player_index', String(playerStore.currentIndex));
}

function resolveTrack(track: Track, quality: string): Promise<ResolveTrackResult> {
  return apiGet<ResolveTrackResult>('/song/url', {
    hash: track.FileHash,
    album_id: track.AlbumID || '',
    album_audio_id: track.AlbumAudioID || '',
    quality,
  });
}

const playbackOrchestrator = new PlaybackOrchestrator({
  backend: () => activeBackend!,
  playSession,
  resolveTrack,
  fetchCover: fetchCoverImage,
  uploadPlayHistory,
  getState: () => playerStore,
  patchState: (patch) => Object.assign(playerStore, patch),
  saveQueue,
});

// Actions
export async function playTrack(track: Track) {
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

export function next() {
  if (playerStore.queue.length === 0) return;

  let nextIdx = playerStore.currentIndex;
  // #11: next() always advances — single-loop replay is handled in the 'ended'
  // handler, not here. Coupling loop semantics to the transient isPlaying flag
  // made UI-next and auto-next disagree.
  if (playerStore.loopMode === 'random') {
    nextIdx = Math.floor(Math.random() * playerStore.queue.length);
  } else {
    nextIdx = (playerStore.currentIndex + 1) % playerStore.queue.length;
  }

  if (nextIdx >= 0 && nextIdx < playerStore.queue.length) {
    playTrack(playerStore.queue[nextIdx]);
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
  localStorage.setItem('player_volume', String(playerStore.volume));
  await activeBackend!.setVolume(playerStore.volume);
}

export function playAll(tracks: Track[], startIndex = 0) {
  playerStore.queue = tracks.map(normalizeTrack);
  playerStore.currentIndex = startIndex;
  saveQueue();
  if (playerStore.queue.length > startIndex) {
    playTrack(playerStore.queue[startIndex]);
  }
}

export function addToQueue(track: Track) {
  const normalized = normalizeTrack(track);
  const exists = playerStore.queue.some(t => t.FileHash === normalized.FileHash);
  if (!exists) {
    playerStore.queue.push(normalized);
    saveQueue();
  }
}

export function removeFromQueue(index: number) {
  if (index < 0 || index >= playerStore.queue.length) return;

  playerStore.queue.splice(index, 1);

  if (playerStore.currentIndex === index) {
    if (playerStore.queue.length === 0) {
      playSession.skip();
      playerStore.currentIndex = -1;
      playerStore.currentTrack = null;
      if (playerStore.audio) {
        playerStore.audio.src = '';
        playerStore.isPlaying = false;
        playerStore.isLoading = false;
      }
    } else {
      playerStore.currentIndex = playerStore.currentIndex % playerStore.queue.length;
      playTrack(playerStore.queue[playerStore.currentIndex]);
    }
  } else if (playerStore.currentIndex > index) {
    playerStore.currentIndex--;
  }

  saveQueue();
}

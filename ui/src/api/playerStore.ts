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
import { recentPlayedStore } from './recentPlayedStore';
import {
  PlaybackOrchestrator,
  type QualityOption,
  type ResolveTrackResult,
} from './playbackOrchestrator';

export type { Track };

export type LoopMode = 'list' | 'single' | 'random';
export type QueueMode = 'normal' | 'personalFm';

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
  queueMode: QueueMode;
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

type BottleMusicAudioGlobal = Window & {
  __bottlemusic_audio__?: HTMLAudioElement;
  __bottlemusic_player_cleanup__?: () => void;
};

let activeBackend: PlayerBackend | null = null;
export let eventUnsub: (() => void) | null = null;
let initListenerCleanup: (() => void) | null = null;
let currentEqSafeSource = '';
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
  webAudioEq.close();
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
  volume: parseFloat(localStorage.getItem('player_volume') || '0.7'),
  queue: loadJSON<Track[]>('player_queue', []),
  currentIndex: parseInt(localStorage.getItem('player_index') || '-1', 10),
  loopMode: (localStorage.getItem('player_loop_mode') || 'list') as LoopMode,
  queueMode: (localStorage.getItem('player_queue_mode') || 'normal') as QueueMode,
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
  retryFailCount: 0,
  retryDisabled: false,
});

/** Test-only seam: tear down the EQ graph between tests. */
export function __resetWebAudioEqForTests() {
  webAudioEq.close();
}

const EQ_UNAVAILABLE_REASON = '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';

const EQ_DEGRADED_REASON = 'EQ 暂不可用，点击重试';

function getAudioSource(audio: HTMLAudioElement): string {
  return audio.getAttribute('src') || audio.currentSrc || audio.src || '';
}

function isLocalAudioProxySource(src: string): boolean {
  if (!src) return false;
  try {
    const url = new URL(src, window.location.href);
    return url.protocol === 'http:'
      && url.hostname === '127.0.0.1'
      && url.pathname.startsWith('/audio/');
  } catch {
    return false;
  }
}

async function preparePlaybackAudioSourceUrl(url: string) {
  if (!playerStore.eqEnabled) {
    return { url, crossOriginSafe: false };
  }
  return prepareAudioSourceUrl(url);
}

function syncEqAvailabilityFromReroute() {
  eqState.available = webAudioEq.isRerouted;
  eqState.reason = eqState.available ? '' : EQ_UNAVAILABLE_REASON;
}

/** Build the long-lived worklet graph once at app startup (spec §5.1). */
export function initWebAudioEQ() {
  webAudioEq.init({
    enabled: playerStore.eqEnabled,
    bands: playerStore.eqBands,
    onDegraded: () => {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
    },
    onRecovered: () => {
      syncEqAvailabilityFromReroute();
    },
  });
}

/** Post-play attach: captureStream → worklet (spec §5.2). Skips when not CORS-safe. */
export async function attachWebAudioEqSource(
  audio: HTMLAudioElement,
  crossOriginSafe = false,
) {
  currentEqSafeSource = crossOriginSafe ? getAudioSource(audio) : '';
  if (!crossOriginSafe) {
    eqState.available = false;
    eqState.reason = EQ_UNAVAILABLE_REASON;
    audio.volume = playerStore.volume;
    return;
  }

  await webAudioEq.awaitReady();
  webAudioEq.attachSource(audio);
  syncEqAvailabilityFromReroute();
  setWebAudioEqVolume(playerStore.volume);
}

export function disconnectWebAudioEqSource() {
  currentEqSafeSource = '';
  webAudioEq.disconnectSource();
  syncEqAvailabilityFromReroute();
}

export function setWebAudioEqVolume(vol: number) {
  webAudioEq.setVolume(vol);
}

export function setWebAudioEqBand(index: number, gainDb: number) {
  webAudioEq.setBand(index, gainDb, playerStore.eqEnabled);
}

export function setWebAudioEqEnabled(enabled: boolean) {
  webAudioEq.setEnabled(enabled, playerStore.eqBands);
  if (!enabled) {
    webAudioEq.disconnectSource();
    if (playerStore.audio) playerStore.audio.volume = playerStore.volume;
    syncEqAvailabilityFromReroute();
    return;
  }
  const audio = playerStore.audio;
  const source = audio ? getAudioSource(audio) : '';
  const sourceSafe = source
    && (source === currentEqSafeSource || isLocalAudioProxySource(source));
  if (audio && !webAudioEq.isRerouted && sourceSafe) {
    void attachWebAudioEqSource(audio, true);
  } else {
    syncEqAvailabilityFromReroute();
  }
}

/** Resume the AudioContext after a user gesture (autoplay policy). */
export function resumeAudioContext() {
  void webAudioEq.resume().catch(() => {
    if (playerStore.audio && webAudioEq.isRerouted) {
      webAudioEq.enterDegradation(playerStore.audio, playerStore.volume);
    }
  });
}

/** Retry EQ after suspend degradation (spec §4.4, §6.3). */
export async function retryEq() {
  if (eqState.retryDisabled || !playerStore.audio) return;
  try {
    await webAudioEq.resume();
    webAudioEq.recoverFromDegradation(playerStore.audio);
    eqState.available = true;
    eqState.reason = '';
    eqState.retryFailCount = 0;
  } catch {
    eqState.retryFailCount++;
    if (eqState.retryFailCount >= 3) {
      eqState.retryDisabled = true;
    }
  }
}

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
    prepareSourceUrl: preparePlaybackAudioSourceUrl,
    getAttachTransitionSeq: () => playbackOrchestrator.getTransitionSeq(),
    isAttachTransitionCurrent: (seq) => playbackOrchestrator.isTransitionCurrent(seq),
    initEq: (audio, crossOriginSafe) => {
      if (!playerStore.eqEnabled) {
        currentEqSafeSource = crossOriginSafe ? getAudioSource(audio) : '';
        eqState.available = false;
        eqState.reason = EQ_UNAVAILABLE_REASON;
        audio.volume = playerStore.volume;
        return;
      }
      void attachWebAudioEqSource(audio, crossOriginSafe);
    },
    disconnectEq: disconnectWebAudioEqSource,
    isEqRerouted: () => webAudioEq.isRerouted,
    setEqVolume: setWebAudioEqVolume,
  });
  playerStore.backend = 'html5';

  eventUnsub = activeBackend.onEvent(handlePlaybackEvent);
}

function handlePlaybackEvent(e: PlaybackEvent) {
  if (e.type === 'position') {
    if (typeof e.position === 'number') {
      playerStore.currentTime = e.position;
      playSession.onTimeUpdate(e.position);
    }
    if (typeof e.duration === 'number' && Number.isFinite(e.duration) && e.duration > 0) {
      playerStore.duration = e.duration;
    }
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
      advanceAfterEnded().catch((err) => console.error('auto-next failed', err));
    }
  } else if (e.type === 'error' && e.error) {
    playerStore.isLoading = false;
    playerStore.errorMsg = e.error;
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
  recordRecentPlayed: (track) => recentPlayedStore.recordRecentPlayed(track),
  getState: () => playerStore,
  patchState: (patch) => Object.assign(playerStore, patch),
  saveQueue,
});

// Actions
export async function playTrack(track: Track) {
  eqState.retryFailCount = 0;
  eqState.retryDisabled = false;
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

function extractSongList(payload: any): any[] {
  const data = payload?.data?.data || payload?.data || payload || {};
  const list = data.song_list || data.info || data.list || data.songs || [];
  return Array.isArray(list) ? list : [];
}

function isPersonalFmFailure(payload: any): boolean {
  return payload?.status === 0;
}

async function fetchPersonalFmRecommendations(query: Record<string, string | number>): Promise<any> {
  let lastResponse: any;
  for (let attempt = 0; attempt < 2; attempt++) {
    lastResponse = await apiGet<any>('/personal/fm', query);
    if (!isPersonalFmFailure(lastResponse)) return lastResponse;
  }
  return lastResponse;
}

async function appendPersonalFmRecommendations(): Promise<boolean> {
  const current = playerStore.currentTrack;
  const remain = Math.max(0, playerStore.queue.length - playerStore.currentIndex - 1);
  const response = await fetchPersonalFmRecommendations({
    hash: current?.FileHash || '',
    songid: current?.AlbumAudioID || current?.MixSongID || '',
    playtime: Math.floor(playerStore.currentTime || 0),
    remain_songcnt: remain,
    is_overplay: remain === 0 ? 1 : 0,
  });
  if (isPersonalFmFailure(response)) {
    console.warn('Personal FM recommendation returned an error:', response?.error || response);
    return false;
  }
  const existing = new Set(playerStore.queue.map((track) => track.FileHash).filter(Boolean));
  const fresh = extractSongList(response)
    .map(normalizeTrack)
    .filter((track) => track.FileHash && !existing.has(track.FileHash));

  if (fresh.length === 0) return false;
  playerStore.queue.push(...fresh);
  saveQueue();
  return true;
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

export function playAll(tracks: Track[], startIndex = 0) {
  playerStore.queue = tracks.map(normalizeTrack);
  playerStore.currentIndex = startIndex;
  playerStore.queueMode = 'normal';
  saveQueue();
  if (playerStore.queue.length > startIndex) {
    playTrack(playerStore.queue[startIndex]);
  }
}

export function playPersonalFm(tracks: Track[], startIndex = 0) {
  playerStore.queue = tracks.map(normalizeTrack);
  playerStore.currentIndex = startIndex;
  playerStore.queueMode = 'personalFm';
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

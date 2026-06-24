import { reactive, watch } from 'vue';
import { apiGet } from './backend';
import { Track, normalizeTrack, fetchCoverImage } from './normalizer';
import { userStore } from './userStore';
import { Html5AudioBackend } from './html5Backend';
// import { NativePlaybackBackend } from './nativeBackend';  // TODO(s4-fix): re-enable after MFS deadlock fix
import type { PlayerBackend, PlaybackEvent } from './playerBackend';
// import { invoke } from '@tauri-apps/api/core';  // unused after HTML5 default switch

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

interface QualityOption {
  quality: string;
  url: string;
  fileSize?: number;
  bitRate?: number;
  extName?: string;
}

let activeBackend: PlayerBackend | null = null;
export let eventUnsub: (() => void) | null = null;

export const playerStore = reactive<PlayerState>({
  currentTrack: null,
  isPlaying: false,
  currentTime: 0,
  duration: 0,
  volume: parseFloat(localStorage.getItem('player_volume') || '0.7'),
  queue: JSON.parse(localStorage.getItem('player_queue') || '[]'),
  currentIndex: parseInt(localStorage.getItem('player_index') || '-1', 10),
  loopMode: (localStorage.getItem('player_loop_mode') || 'list') as LoopMode,
  audio: null,
  errorMsg: '',
  isPreview: false,
  vipRequired: false,
  quality: localStorage.getItem('player_quality') || '128',
  availableQualities: [],
  backend: null,
  eqEnabled: localStorage.getItem('player_eq_enabled') === 'true',
  eqBands: JSON.parse(localStorage.getItem('player_eq_bands') || '[0,0,0,0,0]'),
  activePreset: localStorage.getItem('player_eq_preset') || 'Flat',
});

// Setup audio listeners
export function initPlayer() {
  if (playerStore.audio) return;

  // ── 僵尸音频防护 (Zombie Audio，见 PROJECT_LOGIC §13) ──
  // Vite HMR 热重载会重新求值本模块、生成全新的 playerStore（其 audio 为 null），
  // 而上一个模块实例创建的 <audio> 仍在后台播放 → 多个实例重音、新代码 pause 不掉。
  // 把元素挂到 window 上：每次重载先把上一个彻底销毁，再建新的，保证全局只有一个。
  const g = window as unknown as { __bottlemusic_audio__?: HTMLAudioElement };
  if (g.__bottlemusic_audio__) {
    try {
      const old = g.__bottlemusic_audio__;
      old.pause();
      old.removeAttribute('src');
      old.load();
    } catch { /* ignore */ }
  }

  const audio = new Audio();
  g.__bottlemusic_audio__ = audio;
  playerStore.audio = audio;
  audio.volume = playerStore.volume;

  audio.addEventListener('play', () => {
    playerStore.isPlaying = true;
    playerStore.errorMsg = '';
  });

  audio.addEventListener('pause', () => {
    playerStore.isPlaying = false;
  });

  audio.addEventListener('timeupdate', () => {
    playerStore.currentTime = audio.currentTime;
  });

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

  audio.addEventListener('ended', () => {
    handleEnded();
  });

  audio.addEventListener('error', (e) => {
    console.error('Audio playback error', e);
    playerStore.isPlaying = false;
    playerStore.errorMsg = '播放失败，源文件可能失效或受版权保护';
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
  activeBackend = new Html5AudioBackend(playerStore.audio);
  playerStore.backend = 'html5';

  eventUnsub = activeBackend.onEvent(handlePlaybackEvent);
}

function handlePlaybackEvent(e: PlaybackEvent) {
  if (e.type === 'position') {
    if (typeof e.position === 'number') playerStore.currentTime = e.position;
    if (typeof e.duration === 'number') playerStore.duration = e.duration;
  } else if (e.type === 'state') {
    playerStore.isPlaying = e.state === 'playing';
    playerStore.errorMsg = '';
  } else if (e.type === 'ended') {
    next();
  } else if (e.type === 'error' && e.error) {
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

// Actions
export async function playTrack(track: Track) {
  initPlayer();
  const audio = playerStore.audio!;

  // 立刻停掉当前正在播放的音频：点了新歌就该马上停旧的，
  // 即使新歌取链接失败，也不能让上一首继续在后台响。
  if (activeBackend) {
    await activeBackend.stop().catch(() => {});
  } else {
    audio.pause();
  }
  playerStore.isPlaying = false;

  const normalized = normalizeTrack(track);

  // Find index in queue
  let idx = playerStore.queue.findIndex(t => t.FileHash === normalized.FileHash);
  if (idx === -1) {
    playerStore.queue.push(normalized);
    idx = playerStore.queue.length - 1;
  }
  playerStore.currentIndex = idx;
  playerStore.currentTrack = normalized;
  saveQueue();

  playerStore.errorMsg = '正在加载音频源…';
  playerStore.currentTime = 0;
  playerStore.duration = normalized.Duration || 0;

  // Asynchronously fetch cover if not present
  if (!normalized.Image) {
    fetchCoverImage(normalized.FileHash).then(img => {
      if (img && playerStore.currentTrack?.FileHash === normalized.FileHash) {
        playerStore.currentTrack.Image = img;
        const qIdx = playerStore.queue.findIndex(t => t.FileHash === normalized.FileHash);
        if (qIdx !== -1) {
          playerStore.queue[qIdx].Image = img;
        }
        saveQueue();
      }
    });
  }

  try {
    const res = await apiGet<{
      status: number;
      url?: string;
      error?: string;
      is_preview?: boolean;
      vip_required?: boolean;
      data?: {
        available_qualities?: QualityOption[];
        [key: string]: any;
      };
    }>('/song/url', {
      hash: normalized.FileHash,
      album_id: normalized.AlbumID || '',
      album_audio_id: normalized.AlbumAudioID || '',
      quality: playerStore.quality,
    });

    if (res.status === 1 && res.url) {
      // 存储可用音质选项
      playerStore.availableQualities = res.data?.available_qualities || [];
      
      // 如果有用户选择的音质且可用，切换到该音质
      let finalUrl = res.url;
      if (playerStore.quality && playerStore.availableQualities.length > 0) {
        const preferred = playerStore.availableQualities.find(q => q.quality === playerStore.quality);
        if (preferred?.url) {
          finalUrl = preferred.url;
        }
      }
      
      // Set preview state BEFORE playUrl so backend events don't clobber it.
      playerStore.isPreview = !!res.is_preview;
      playerStore.vipRequired = !!res.vip_required;
      playerStore.errorMsg = '';

      if (activeBackend) {
        const ok = await activeBackend.playUrl(finalUrl);
        if (!ok) {
          playerStore.isPlaying = false;
          playerStore.errorMsg = '播放失败';
          return;
        }
      } else {
        // Legacy fallback: direct audio element
        audio.src = finalUrl;
        await audio.play();
      }

      // 播放成功后异步上传播放历史（静默失败，不阻塞播放）
      uploadPlayHistory(normalized);
    } else {
      playerStore.isPreview = false;
      playerStore.vipRequired = false;
      throw new Error(res.error || '获取歌曲链接失败');
    }
  } catch (err: any) {
    console.error('Failed to resolve play URL', err);
    // 取链接失败：彻底清掉音频源，避免之后点"播放"又恢复上一首。
    if (activeBackend) {
      await activeBackend.stop().catch(() => {});
    } else {
      audio.removeAttribute('src');
      audio.load();
    }
    playerStore.isPlaying = false;
    playerStore.isPreview = false;
    playerStore.vipRequired = false;
    playerStore.errorMsg = err.message || '该歌曲不可播放（可能是 Demo / 版权或 VIP 限制）';
  }
}

// 音质切换请求序号，用于防止快速连续切换导致的竞态
let qualityRequestId = 0;

/** 切换音质等级 */
export function setQuality(quality: string) {
  playerStore.quality = quality;
  localStorage.setItem('player_quality', quality);
  
  // 如果当前有歌曲在播放，尝试切换到新音质
  if (playerStore.currentTrack) {
    // 先检查缓存的 availableQualities 中是否有目标音质
    if (playerStore.availableQualities.length > 0) {
      const preferred = playerStore.availableQualities.find(q => q.quality === quality);
      if (preferred?.url && playerStore.audio) {
        const wasPlaying = playerStore.isPlaying;
        const savedTime = playerStore.currentTime;
        playerStore.audio.src = preferred.url;
        if (savedTime > 0) playerStore.audio.currentTime = savedTime;
        if (wasPlaying) {
          playerStore.audio.play().catch(e => console.error('Quality switch play failed', e));
        }
        return;
      }
    }
    // 缓存中没有目标音质，重新请求（playTrack 会使用新的 quality 参数）
    // 保存播放进度，请求完成后恢复
    const reqId = ++qualityRequestId;
    const savedTime = playerStore.currentTime;
    const wasPlaying = playerStore.isPlaying;
    playTrack(playerStore.currentTrack).then(() => {
      if (reqId !== qualityRequestId) return; // 已被更新的请求取代
      if (playerStore.audio && savedTime > 0) {
        playerStore.audio.currentTime = savedTime;
        if (wasPlaying) playerStore.audio.play().catch(() => {});
      }
    });
  }
}

export async function togglePlay() {
  if (!playerStore.currentTrack) return;

  if (activeBackend) {
    if (playerStore.isPlaying) {
      await activeBackend.pause();
    } else {
      await activeBackend.resume();
    }
  } else {
    // Legacy fallback
    initPlayer();
    if (!playerStore.audio) return;
    if (playerStore.isPlaying) {
      playerStore.audio.pause();
    } else {
      if (!playerStore.audio.src) {
        playTrack(playerStore.currentTrack);
      } else {
        playerStore.audio.play().catch(e => {
          console.error('Play failed', e);
        });
      }
    }
  }
}

export function next() {
  if (playerStore.queue.length === 0) return;

  let nextIdx = playerStore.currentIndex;
  if (playerStore.loopMode === 'single' && playerStore.isPlaying) {
    // Keep index
  } else if (playerStore.loopMode === 'random') {
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
  if (activeBackend) {
    await activeBackend.seek(seconds);
  } else if (playerStore.audio) {
    playerStore.audio.currentTime = seconds;
  }
  playerStore.currentTime = seconds;
}

export async function setVolume(vol: number) {
  playerStore.volume = Math.max(0, Math.min(1, vol));
  localStorage.setItem('player_volume', String(playerStore.volume));
  if (activeBackend) {
    await activeBackend.setVolume(playerStore.volume);
  }
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
      playerStore.currentIndex = -1;
      playerStore.currentTrack = null;
      if (playerStore.audio) {
        playerStore.audio.src = '';
        playerStore.isPlaying = false;
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

function handleEnded() {
  if (playerStore.loopMode === 'single') {
    if (playerStore.audio) {
      playerStore.audio.currentTime = 0;
      playerStore.audio.play().catch(e => console.error(e));
    }
  } else {
    next();
  }
}

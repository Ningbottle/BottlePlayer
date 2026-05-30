import { reactive, watch } from 'vue';
import { apiGet } from './backend';
import { Track, normalizeTrack, fetchCoverImage } from './normalizer';

export type { Track };


export type LoopMode = 'list' | 'single' | 'random';

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
  // True when the current track's URL is the 60s free preview, not the full song.
  // Stays true across pause/play; only resets when a new track is loaded.
  isPreview: boolean;
  // True specifically when KuGou rejected the request because the account has
  // no VIP entitlement (fail_process contains "pkg"/"buy"). vipRequired implies
  // isPreview but adds the "you need VIP" semantic so the UI can say so
  // explicitly instead of the generic "试听版本" hedge.
  vipRequired: boolean;
}

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

// Watch volume and queue to persist
watch(() => playerStore.volume, (newVol) => {
  localStorage.setItem('player_volume', String(newVol));
  if (playerStore.audio) {
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
  audio.pause();
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
    }>('/song/url', {
      hash: normalized.FileHash,
      album_id: normalized.AlbumID || '',
      album_audio_id: normalized.AlbumAudioID || '',
    });

    if (res.status === 1 && res.url) {
      audio.src = res.url;
      // Set preview state BEFORE play() so the 'play' event listener doesn't
      // clobber it. The listener only clears `errorMsg`, not `isPreview`.
      playerStore.isPreview = !!res.is_preview;
      playerStore.vipRequired = !!res.vip_required;
      playerStore.errorMsg = '';
      await audio.play();
    } else {
      playerStore.isPreview = false;
      playerStore.vipRequired = false;
      throw new Error(res.error || '获取歌曲链接失败');
    }
  } catch (err: any) {
    console.error('Failed to resolve play URL', err);
    // 取链接失败：彻底清掉音频源，避免之后点“播放”又恢复上一首。
    audio.removeAttribute('src');
    audio.load();
    playerStore.isPlaying = false;
    playerStore.isPreview = false;
    playerStore.vipRequired = false;
    playerStore.errorMsg = err.message || '该歌曲不可播放（可能是 Demo / 版权或 VIP 限制）';
  }
}

export function togglePlay() {
  initPlayer();
  if (!playerStore.audio || !playerStore.currentTrack) return;

  if (playerStore.isPlaying) {
    playerStore.audio.pause();
  } else {
    // If src is empty (restored state), reload URL
    if (!playerStore.audio.src) {
      playTrack(playerStore.currentTrack);
    } else {
      playerStore.audio.play().catch(e => {
        console.error('Play failed', e);
      });
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

export function seek(seconds: number) {
  if (playerStore.audio) {
    playerStore.audio.currentTime = seconds;
    playerStore.currentTime = seconds;
  }
}

export function setVolume(vol: number) {
  playerStore.volume = Math.max(0, Math.min(1, vol));
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

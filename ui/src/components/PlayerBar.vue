<script setup lang="ts">
import { computed } from 'vue';
import { playerStore, togglePlay, next, prev, seek, setVolume } from '../api/playerStore';

const props = defineProps<{
  activeView: string;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string): void;
  (e: 'toggle-queue'): void;
}>();

const currentTrack = computed(() => playerStore.currentTrack);
const isPlaying = computed(() => playerStore.isPlaying);
const currentTime = computed(() => playerStore.currentTime);
const duration = computed(() => playerStore.duration);
const volume = computed(() => playerStore.volume);
const loopMode = computed(() => playerStore.loopMode);
const errorMsg = computed(() => playerStore.errorMsg);
const isPreview = computed(() => playerStore.isPreview);

function formatTime(sec: number) {
  if (isNaN(sec) || sec === null || sec === undefined) return '00:00';
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

const progressPercent = computed(() => {
  if (!duration.value) return 0;
  return (currentTime.value / duration.value) * 100;
});

const volumePercent = computed(() => {
  return volume.value * 100;
});

function handleSeek(e: MouseEvent) {
  if (!duration.value) return;
  const trackEl = e.currentTarget as HTMLElement;
  const rect = trackEl.getBoundingClientRect();
  const clickX = e.clientX - rect.left;
  const pct = Math.max(0, Math.min(1, clickX / rect.width));
  seek(pct * duration.value);
}

function handleVolumeClick(e: MouseEvent) {
  const barEl = e.currentTarget as HTMLElement;
  const rect = barEl.getBoundingClientRect();
  const clickX = e.clientX - rect.left;
  const pct = Math.max(0, Math.min(1, clickX / rect.width));
  setVolume(pct);
}

function toggleLoopMode() {
  const modes: ('list' | 'single' | 'random')[] = ['list', 'single', 'random'];
  const nextIdx = (modes.indexOf(loopMode.value) + 1) % modes.length;
  playerStore.loopMode = modes[nextIdx];
}

function toggleLyricView() {
  if (props.activeView === 'lyric') {
    emit('navigate', 'home');
  } else {
    emit('navigate', 'lyric');
  }
}
</script>

<template>
  <footer class="player">
    <!-- Left: Track info -->
    <div class="np" @click="toggleLyricView" style="cursor: pointer;" title="点击查看歌词 · Click to view lyrics">
      <div class="cv">
        <template v-if="currentTrack">
          <img v-if="currentTrack.Image" :src="currentTrack.Image" alt="cover" />
          <svg v-else viewBox="0 0 56 56">
            <rect width="56" height="56" fill="#2a2520"/>
            <text x="28" y="32" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="10" fill="#f1ead8">
              {{ currentTrack.SongName.slice(0, 6) }}
            </text>
          </svg>
        </template>
        <svg v-else viewBox="0 0 56 56">
          <rect width="56" height="56" fill="#2a2520"/>
          <text x="28" y="32" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="10" fill="#f1ead8">
            未播放
          </text>
        </svg>
      </div>

      <div class="info">
        <template v-if="currentTrack">
          <b>{{ currentTrack.SongName }}</b>
          <span>{{ currentTrack.SingerName }}</span>
        </template>
        <template v-else>
          <b>未播放歌曲</b>
          <span>— —</span>
        </template>
      </div>
      
      <!-- Error / status message overlay -->
      <span v-if="errorMsg" class="dim" style="font-size: 11px; margin-left: 10px; color: var(--accent);">
        {{ errorMsg }}
      </span>
      <!-- Preview-mode banner — sticks across pause/play, only clears on next track -->
      <span v-else-if="isPreview" class="dim" style="font-size: 11px; margin-left: 10px; color: var(--accent);">
        ⚠️ 试听 60 秒 (VIP 歌曲)
      </span>
    </div>

    <!-- Center: Transport controls -->
    <div class="transport">
      <div class="row">
        <!-- Loop Mode random -->
        <button 
          class="t-btn" 
          :style="{ color: loopMode === 'random' ? 'var(--accent)' : 'inherit' }"
          aria-label="shuffle" 
          @click="toggleLoopMode"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M16 3h5v5M4 20l17-17M21 16v5h-5M15 15l6 6M4 4l5 5"/>
          </svg>
        </button>

        <!-- Prev -->
        <button class="t-btn" aria-label="prev" @click="prev">
          <svg viewBox="0 0 24 24" fill="currentColor">
            <polygon points="6,5 6,19 8,19 8,13 19,19 19,5 8,11 8,5"/>
          </svg>
        </button>

        <!-- Play/Pause -->
        <button class="t-btn play" aria-label="play" @click="togglePlay">
          <svg v-if="isPlaying" viewBox="0 0 24 24" fill="currentColor">
            <rect x="6" y="4" width="4" height="16" />
            <rect x="14" y="4" width="4" height="16" />
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="currentColor">
            <polygon points="6,4 20,12 6,20"/>
          </svg>
        </button>

        <!-- Next -->
        <button class="t-btn" aria-label="next" @click="next">
          <svg viewBox="0 0 24 24" fill="currentColor">
            <polygon points="5,5 16,11 16,5 18,5 18,19 16,19 16,13 5,19"/>
          </svg>
        </button>

        <!-- Loop Mode repeat -->
        <button 
          class="t-btn"
          :style="{ color: loopMode === 'single' ? 'var(--accent)' : 'inherit' }"
          aria-label="repeat" 
          @click="toggleLoopMode"
        >
          <svg v-if="loopMode === 'single'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3"/>
            <text x="12" y="15" font-size="8" font-weight="900" fill="currentColor" stroke="none" text-anchor="middle">1</text>
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3"/>
          </svg>
        </button>
      </div>

      <!-- Seek Bar -->
      <div class="seek">
        <span class="time">{{ formatTime(currentTime) }}</span>
        <div class="track" @click="handleSeek">
          <div class="rule"></div>
          <div class="fill" :style="{ width: progressPercent + '%' }"></div>
          <div class="nib" :style="{ left: progressPercent + '%' }"></div>
        </div>
        <span class="time">{{ formatTime(duration) }}</span>
      </div>
    </div>

    <!-- Right: Volume & extra utilities -->
    <div class="player-right">
      <button class="quality">高质量<span class="tag">全景声</span></button>
      <button class="p-icon queue" aria-label="queue" @click="emit('toggle-queue')" title="播放队列">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="14" height="14">
          <path d="M8 6h13 M8 12h13 M8 18h13 M3 6h.01 M3 12h.01 M3 18h.01"/>
        </svg>
      </button>
      <button 
        class="p-icon lyric" 
        :class="{ active: activeView === 'lyric' }" 
        aria-label="lyric"
        @click="toggleLyricView"
      >
        词
      </button>
      
      <!-- Volume control -->
      <div class="volume">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <path d="M11 5L6 9H2v6h4l5 4z M15 9a4 4 0 0 1 0 6 M18 6a8 8 0 0 1 0 12"/>
        </svg>
        <div class="bar" @click="handleVolumeClick">
          <div class="fill" :style="{ width: volumePercent + '%' }"></div>
        </div>
      </div>
    </div>
  </footer>
</template>

<style scoped>
/* Scoped overrides */
</style>

<script setup lang="ts">
import { computed, ref } from 'vue';
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
const vipRequired = computed(() => playerStore.vipRequired);

// Stable fallback cover — keeps the <img> element mounted even when the
// track has no Image yet (avoids the v-if mount/unmount flicker during
// track switches). Uses paper-colored background so the swap is invisible.
const FALLBACK_COVER =
  'data:image/svg+xml;utf8,' + encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 56 56">` +
    `<rect width="56" height="56" fill="#2a2520"/>` +
    `<text x="28" y="34" text-anchor="middle" font-family="Noto Serif SC,serif" ` +
    `font-weight="700" font-size="14" fill="#f1ead8">听</text></svg>`
  );

const coverUrl = computed(() => currentTrack.value?.Image || FALLBACK_COVER);

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

// Toast notification for mode changes
const toastMsg = ref('');
let toastTimer: any = null;
function showToast(msg: string) {
  toastMsg.value = msg;
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toastMsg.value = ''; }, 2000);
}

// 随机 / 单曲循环 是互斥三态 loopMode 的两个独立开关：
// 随机键在 random ⇄ list 间切换；循环键在 single ⇄ list 间切换。
// （旧的 toggleLoopMode 让两个按钮都三态轮转，极易误入 random 导致“下一首”乱跳。）
function toggleShuffle() {
  const isRandom = loopMode.value === 'random';
  playerStore.loopMode = isRandom ? 'list' : 'random';
  showToast(isRandom ? '已切换为 列表顺序播放' : '已切换为 随机播放');
}
function toggleRepeat() {
  const isSingle = loopMode.value === 'single';
  playerStore.loopMode = isSingle ? 'list' : 'single';
  showToast(isSingle ? '已切换为 列表顺序播放' : '已切换为 单曲循环');
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
    <!-- Mode Toast Message -->
    <transition name="toast-fade">
      <div v-if="toastMsg" class="mode-toast">
        {{ toastMsg }}
      </div>
    </transition>

    <!-- Left: Track info -->
    <div class="np" @click="toggleLyricView" style="cursor: pointer;" title="点击查看歌词 · Click to view lyrics">
      <div class="cv">
        <!-- Stable <img> with fallback data URL — avoids v-if remount flicker
             when switching tracks while a cover is still loading. -->
        <img :src="coverUrl" alt="cover" style="transition: opacity 0.15s ease;" />
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
      <!-- Two-tier preview banner:
           - vipRequired: KuGou explicitly returned fail_process:["pkg","buy"]
             meaning this song needs VIP and the account has none. Show the
             specific call-to-action so the user knows what to do.
           - isPreview (without vipRequired): some other reason — auth fallback,
             rate limit, region — fall back to the generic phrasing. -->
      <span v-else-if="vipRequired" class="dim" style="font-size: 11px; margin-left: 10px; color: var(--accent);">
        ⚠️ VIP 歌曲 · 仅 60s 试听（需要 VIP 才能完整播放）
      </span>
      <span v-else-if="isPreview" class="dim" style="font-size: 11px; margin-left: 10px; color: var(--ink-soft);">
        ⚠️ 试听版本（KuGou 仅授权部分时长）
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
          title="随机播放"
          @click="toggleShuffle"
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
          title="单曲循环"
          @click="toggleRepeat"
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
.mode-toast {
  position: absolute;
  top: -48px; /* Appears above the player bar */
  left: 50%;
  transform: translateX(-50%);
  background: var(--ink);
  color: var(--paper);
  padding: 8px 16px;
  border-radius: 20px;
  font-size: 13px;
  font-weight: 500;
  box-shadow: 0 4px 12px rgba(40,28,12,0.25);
  pointer-events: none; /* Let clicks pass through */
  z-index: 1000;
}

.toast-fade-enter-active,
.toast-fade-leave-active {
  transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
}
.toast-fade-enter-from,
.toast-fade-leave-to {
  opacity: 0;
  transform: translate(-50%, 10px);
}

/* Scoped overrides */
</style>

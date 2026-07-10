<script setup lang="ts">
import { computed, ref } from 'vue';
import { playerStore, togglePlay, next, prev, seek, setVolume, setQuality } from '../api/playerStore';
import AddToPlaylistModal from './AddToPlaylistModal.vue';

const props = defineProps<{
  activeView: string;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string): void;
  (e: 'toggle-queue'): void;
}>();

// 收藏功能
const showAddModal = ref(false);
const favoriteMsg = ref('');
let favToastTimer: ReturnType<typeof setTimeout> | null = null;

function handleFavorite() {
  if (!currentTrack.value) return;
  showAddModal.value = true;
}

function handleFavoriteSuccess(playlistName: string) {
  favoriteMsg.value = `已收藏到「${playlistName}」`;
  if (favToastTimer) clearTimeout(favToastTimer);
  favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
}

function handleFavoriteError(msg: string) {
  favoriteMsg.value = msg;
  if (favToastTimer) clearTimeout(favToastTimer);
  favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
}

const currentTrack = computed(() => playerStore.currentTrack);
const isPlaying = computed(() => playerStore.isPlaying);
const isLoading = computed(() => playerStore.isLoading);
const showPauseIcon = computed(() => isPlaying.value || isLoading.value);
const currentTime = computed(() => playerStore.currentTime);
const duration = computed(() => playerStore.duration);
const volume = computed(() => playerStore.volume);
const loopMode = computed(() => playerStore.loopMode);
const errorMsg = computed(() => playerStore.errorMsg);
const isPreview = computed(() => playerStore.isPreview);
const vipRequired = computed(() => playerStore.vipRequired);
const quality = computed(() => playerStore.quality);

// 音质选择器
const showQualityMenu = ref(false);
const qualityLabels: Record<string, string> = {
  '128': '标准',
  '320': '高品',
  'flac': '无损',
  'hires': 'Hi-Res',
  'master': '臻品',
};
// 预定义的可选音质列表（用户可选择，切换时重新请求）
const qualityOptions = ['128', '320', 'flac'];
function getQualityLabel(q: string): string {
  return qualityLabels[q] || q;
}
function isCurrentQuality(q: string): boolean {
  return quality.value === q;
}
function handleSelectQuality(q: string) {
  if (isCurrentQuality(q)) return; // 已经是当前音质，不切换
  setQuality(q);
  showQualityMenu.value = false;
}
// 点击外部关闭菜单
function closeQualityMenu() {
  showQualityMenu.value = false;
}

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
  <footer class="player" @click="closeQualityMenu">
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
        <img :src="coverUrl" alt="cover" style="transition: opacity 0.15s var(--ease-spa);" />
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

      <!-- 收藏按钮 -->
      <button 
        v-if="currentTrack"
        class="p-icon fav-inline" 
        aria-label="favorite"
        title="收藏"
        @click.stop="handleFavorite"
      >
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="16" height="16">
          <path d="M12 2l2.39 6.96H22l-6 4.62L18.18 21 12 16.77 5.82 21 8 13.58 2 8.96h7.61z"/>
        </svg>
      </button>
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
        <button
          class="t-btn play"
          :aria-label="showPauseIcon ? 'pause' : 'play'"
          :title="isLoading ? '取消加载' : (isPlaying ? '暂停' : '播放')"
          @click="togglePlay"
        >
          <svg v-if="showPauseIcon" viewBox="0 0 24 24" fill="currentColor">
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
      <!-- 音质选择器 -->
      <div class="quality-selector" @click.stop>
        <button 
          class="quality" 
          :class="{ active: showQualityMenu }"
          @click="showQualityMenu = !showQualityMenu"
          title="音质选择"
        >
          {{ getQualityLabel(quality) }}
          <span class="tag">
            切换
          </span>
        </button>
        
        <!-- 音质下拉菜单 -->
        <transition name="menu-fade">
          <div v-if="showQualityMenu" class="quality-menu" @click="closeQualityMenu">
            <div 
              v-for="q in qualityOptions" 
              :key="q"
              class="quality-option"
              :class="{ active: isCurrentQuality(q) }"
              @click="handleSelectQuality(q)"
            >
              <span class="q-label">{{ getQualityLabel(q) }}</span>
              <span v-if="isCurrentQuality(q)" class="q-detail">
                当前
              </span>
            </div>
          </div>
        </transition>
      </div>
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

    <!-- 收藏成功提示 -->
    <transition name="toast-fade">
      <div v-if="favoriteMsg" class="mode-toast" style="top: -48px;">
        {{ favoriteMsg }}
      </div>
    </transition>

    <!-- 收藏到歌单弹窗 -->
    <AddToPlaylistModal
      :show="showAddModal"
      :track="currentTrack"
      @close="showAddModal = false"
      @success="handleFavoriteSuccess"
      @error="handleFavoriteError"
    />
  </footer>
</template>

<style scoped>
.p-icon.fav-inline {
  background: none;
  border: none;
  padding: 4px;
  margin-left: 10px;
  cursor: pointer;
  color: var(--ink-mute, #8a7e6a);
  transition: color 0.2s;
  vertical-align: middle;
  display: inline-flex;
  align-items: center;
}
.p-icon.fav-inline:hover {
  color: var(--accent, #a8311b);
}

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
:global(:root[data-mode="dark"]) .mode-toast {
  box-shadow: 0 4px 12px rgba(0,0,0,0.45);
}

.toast-fade-enter-active,
.toast-fade-leave-active {
  transition: all 0.3s var(--ease-spa);
}
.toast-fade-enter-from,
.toast-fade-leave-to {
  opacity: 0;
  transform: translate(-50%, 10px);
}

/* Scoped overrides */

/* Quality selector */
.quality-selector {
  position: relative;
}

.quality {
  font-family: 'Noto Serif SC', serif;
  font-size: 12px;
  padding: 4px 10px;
  border: 1px solid var(--ink-soft, #666);
  border-radius: 4px;
  background: transparent;
  color: var(--ink);
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: all 0.2s;
}

.quality:hover,
.quality.active {
  border-color: var(--accent, #a8311b);
  color: var(--accent, #a8311b);
}

.quality .tag {
  font-size: 10px;
  padding: 1px 4px;
  border-radius: 2px;
  background: var(--accent, #a8311b);
  color: var(--paper, #f1ead8);
  line-height: 1;
}

/* Quality dropdown menu */
.quality-menu {
  position: absolute;
  bottom: 100%;
  right: 0;
  margin-bottom: 8px;
  background: var(--paper, #f1ead8);
  border: 1px solid var(--ink-soft, #666);
  border-radius: 8px;
  padding: 6px 0;
  min-width: 140px;
  box-shadow: 0 8px 24px rgba(40, 28, 12, 0.2);
  z-index: 1001;
}

:global(:root[data-mode="dark"]) .quality-menu {
  background: var(--ink, #2a2520);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
}

.quality-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  cursor: pointer;
  transition: background 0.15s;
  font-size: 13px;
}

.quality-option:hover {
  background: var(--ink-soft-10, rgba(102, 102, 102, 0.1));
}

.quality-option.active {
  color: var(--accent, #a8311b);
  font-weight: 600;
}

.quality-option .q-detail {
  font-size: 11px;
  color: var(--ink-soft, #666);
  margin-left: 8px;
}

.quality-empty {
  padding: 12px 14px;
  text-align: center;
  color: var(--ink-soft, #666);
  font-size: 12px;
}

/* Menu fade transition */
.menu-fade-enter-active,
.menu-fade-leave-active {
  transition: all 0.2s var(--ease-spa);
}

.menu-fade-enter-from,
.menu-fade-leave-to {
  opacity: 0;
  transform: translateY(4px);
}
</style>

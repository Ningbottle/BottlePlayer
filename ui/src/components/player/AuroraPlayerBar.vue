<script setup lang="ts">
import { computed } from 'vue';
import type { PlayerController } from './usePlayerControls';
import PlayerProgress from './PlayerProgress.vue';
import { animateElement } from '../../api/motion';

const props = defineProps<{
  controller: PlayerController;
}>();

const emit = defineEmits<{
  (e: 'toggle-queue'): void;
}>();

const c = computed(() => props.controller);

function handleVolumeClick(e: MouseEvent) {
  const barEl = e.currentTarget as HTMLElement;
  const rect = barEl.getBoundingClientRect();
  const clickX = e.clientX - rect.left;
  const pct = Math.max(0, Math.min(1, clickX / rect.width));
  c.value.setVolume(pct);
}

function onPress(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  animateElement(el, { scale: 1 }, { scale: 0.92 }, 'controlPress');
}

function onRelease(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  animateElement(el, { scale: 0.92 }, { scale: 1 }, 'controlRelease');
}
</script>

<template>
  <footer class="aurora-pb" @click="c.closeQualityMenu">
    <!-- Mode Toast -->
    <transition name="toast-fade">
      <div v-if="c.toastMsg" class="mode-toast aurora-pb-toast">
        {{ c.toastMsg }}
      </div>
    </transition>

    <!-- Favorite toast -->
    <transition name="toast-fade">
      <div v-if="c.favoriteMsg" class="mode-toast aurora-pb-toast" style="top: -48px;">
        {{ c.favoriteMsg }}
      </div>
    </transition>

    <!-- Left: cover + metadata -->
    <div class="aurora-pb-left" @click="c.toggleLyricView" style="cursor: pointer;" title="点击查看歌词">
      <div class="aurora-pb-cover">
        <img :src="c.coverUrl" alt="cover" />
      </div>

      <div class="aurora-pb-info">
        <template v-if="c.currentTrack">
          <b>{{ c.currentTrack.SongName }}</b>
          <span>{{ c.currentTrack.SingerName }}</span>
        </template>
        <template v-else>
          <b>未播放歌曲</b>
          <span>- -</span>
        </template>
      </div>

      <span v-if="c.errorMsg" class="aurora-pb-status" style="color: var(--accent);">
        {{ c.errorMsg }}
      </span>
      <span v-else-if="c.vipRequired" class="aurora-pb-status" style="color: var(--accent);">
        ⚠️ VIP 歌曲 · 仅 60s 试听
      </span>
      <span v-else-if="c.isPreview" class="aurora-pb-status">
        ⚠️ 试听版本
      </span>

      <button
        v-if="c.currentTrack"
        class="aurora-pb-fav"
        aria-label="favorite"
        title="收藏"
        @click.stop="c.handleFavorite"
      >
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="16" height="16">
          <path d="M12 2l2.39 6.96H22l-6 4.62L18.18 21 12 16.77 5.82 21 8 13.58 2 8.96h7.61z"/>
        </svg>
      </button>
    </div>

    <!-- Center: liquid console with transport + progress -->
    <div class="aurora-pb-center" data-test="aurora-player-console">
      <div class="aurora-pb-transport" role="group" aria-label="播放控制">
        <button
          class="aurora-pb-btn"
          :style="{ color: c.loopMode === 'random' ? 'var(--accent)' : 'inherit' }"
          aria-label="shuffle"
          title="随机播放"
          @click="c.toggleShuffle"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M16 3h5v5M4 20l17-17M21 16v5h-5M15 15l6 6M4 4l5 5"/>
          </svg>
        </button>

        <button
          class="aurora-pb-btn"
          aria-label="prev"
          @click="c.prev"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="currentColor">
            <polygon points="6,5 6,19 8,19 8,13 19,19 19,5 8,11 8,5"/>
          </svg>
        </button>

        <button
          class="aurora-pb-btn aurora-pb-play"
          :aria-label="c.showPauseIcon ? 'pause' : 'play'"
          :title="c.isLoading ? '取消加载' : (c.isPlaying ? '暂停' : '播放')"
          @click="c.togglePlay"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg v-if="c.showPauseIcon" viewBox="0 0 24 24" fill="currentColor">
            <rect x="6" y="4" width="4" height="16" />
            <rect x="14" y="4" width="4" height="16" />
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="currentColor">
            <polygon points="6,4 20,12 6,20"/>
          </svg>
        </button>

        <button
          class="aurora-pb-btn"
          aria-label="next"
          @click="c.next"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="currentColor">
            <polygon points="5,5 16,11 16,5 18,5 18,19 16,19 16,13 5,19"/>
          </svg>
        </button>

        <button
          class="aurora-pb-btn"
          :style="{ color: c.loopMode === 'single' ? 'var(--accent)' : 'inherit' }"
          aria-label="repeat"
          title="单曲循环"
          @click="c.toggleRepeat"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg v-if="c.loopMode === 'single'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3"/>
            <text x="12" y="15" font-size="8" font-weight="900" fill="currentColor" stroke="none" text-anchor="middle">1</text>
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
            <path d="M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3"/>
          </svg>
        </button>
      </div>

      <div class="aurora-pb-progress-wrap" data-test="aurora-player-progress">
        <PlayerProgress
          :current-time="c.currentTime"
          :duration="c.duration"
          @seek="c.seek"
        />
      </div>
    </div>

    <!-- Right: quality / queue / lyric / volume -->
    <div class="aurora-pb-right">
      <div class="aurora-pb-quality" @click.stop>
        <button
          class="aurora-pb-q-btn"
          :class="{ active: c.showQualityMenu }"
          @click="c.showQualityMenu = !c.showQualityMenu"
          title="音质选择"
        >
          {{ c.getQualityLabel(c.quality) }}
          <span class="aurora-pb-q-tag">切换</span>
        </button>

        <transition name="menu-fade">
          <div v-if="c.showQualityMenu" class="aurora-pb-q-menu" @click="c.closeQualityMenu">
            <div
              v-for="q in c.qualityOptions"
              :key="q"
              class="aurora-pb-q-option"
              :class="{ active: c.isCurrentQuality(q) }"
              @click="c.handleSelectQuality(q)"
            >
              <span>{{ c.getQualityLabel(q) }}</span>
              <span v-if="c.isCurrentQuality(q)" class="aurora-pb-q-current">当前</span>
            </div>
          </div>
        </transition>
      </div>

      <button class="aurora-pb-icon" aria-label="queue" @click="emit('toggle-queue')" title="播放队列">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="14" height="14">
          <path d="M8 6h13 M8 12h13 M8 18h13 M3 6h.01 M3 12h.01 M3 18h.01"/>
        </svg>
      </button>

      <button
        class="aurora-pb-icon aurora-pb-lyric"
        :class="{ active: c.isLyricView }"
        aria-label="lyric"
        title="歌词"
        @click="c.toggleLyricView"
      >
        词
      </button>

      <div class="aurora-pb-volume">
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <path d="M11 5L6 9H2v6h4l5 4z M15 9a4 4 0 0 1 0 6 M18 6a8 8 0 0 1 0 12"/>
        </svg>
        <div class="aurora-pb-vol-bar" @click="handleVolumeClick">
          <div class="aurora-pb-vol-fill" :style="{ width: c.volumePercent + '%' }"></div>
        </div>
      </div>
    </div>
  </footer>
</template>

<style scoped>
.aurora-pb {
  position: relative;
  display: grid;
  grid-template-columns: minmax(230px, 0.8fr) minmax(420px, 1.45fr) minmax(250px, 0.8fr);
  gap: 18px;
  align-items: center;
  min-height: 114px;
  padding: 12px 22px;
  box-sizing: border-box;
  border: 1px solid color-mix(in srgb, var(--text-primary) 9%, transparent);
  border-radius: 34px 34px 28px 28px;
  /* WebView fallback before color-mix */
  background: var(--surface-elevated);
  background: color-mix(in srgb, var(--surface-elevated) 86%, transparent);
  box-shadow: 0 20px 46px rgba(0, 0, 0, 0.26), inset 0 1px rgba(255, 255, 255, 0.08);
}

.aurora-pb-left {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
  position: relative;
}

.aurora-pb-cover {
  width: 56px;
  height: 56px;
  border-radius: 10px;
  border: 1px solid var(--border-subtle, rgba(34,27,18,0.12));
  overflow: hidden;
  flex: none;
  box-shadow: 0 2px 6px rgba(40,28,12,0.2);
  background: var(--surface-1, #f1ead8);
}

.aurora-pb-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition: opacity 0.15s var(--ease-spa, ease);
}

.aurora-pb-info {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.aurora-pb-info b {
  font-size: 14px;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.aurora-pb-info span {
  font-size: 12px;
  color: var(--ink-soft, #8a7e6a);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.aurora-pb-status {
  font-size: 11px;
  margin-left: 10px;
  color: var(--ink-soft, #8a7e6a);
  white-space: nowrap;
}

.aurora-pb-fav {
  background: none;
  border: none;
  padding: 4px;
  margin-left: 10px;
  cursor: pointer;
  color: var(--ink-mute, #8a7e6a);
  transition: color 0.2s;
  display: inline-flex;
  align-items: center;
}

.aurora-pb-fav:hover {
  color: var(--accent, #a8311b);
}

.aurora-pb-center {
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-self: stretch;
  margin: -20px 0 0;
  padding: 14px 28px 10px;
  border-radius: 46% 46% 30px 30px;
  /* WebView fallback before color-mix */
  background: var(--surface-2);
  background: color-mix(in srgb, var(--surface-2) 74%, transparent);
}

.aurora-pb-progress-wrap {
  width: 100%;
  min-width: 0;
}

.aurora-pb-transport {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 22px;
}

.aurora-pb-btn {
  width: 30px;
  height: 30px;
  display: grid;
  place-items: center;
  cursor: pointer;
  color: var(--ink-soft, #666);
  background: transparent;
  border: none;
  border-radius: 50%;
  transition: transform 0.1s ease-out, color 0.2s;
}

.aurora-pb-btn:hover {
  color: var(--ink, #2a2520);
}

.aurora-pb-btn svg {
  width: 18px;
  height: 18px;
}

.aurora-pb-play {
  width: 38px;
  height: 38px;
  background: var(--ink, #2a2520);
  color: var(--paper, #f1ead8);
  border-radius: 50%;
  box-shadow: 0 2px 6px rgba(40,28,12,0.3), inset 0 1px 0 rgba(255,252,243,0.18);
  transition: transform 0.3s cubic-bezier(0.34, 1.56, 0.64, 1), background 0.2s;
}

.aurora-pb-play:hover {
  background: #000;
}

.aurora-pb-play svg {
  width: 16px;
  height: 16px;
}

.aurora-pb-right {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 10px;
  color: var(--ink-soft, #666);
}

.aurora-pb-quality {
  position: relative;
}

.aurora-pb-q-btn {
  font-size: 12px;
  padding: 4px 10px;
  border: 1px solid var(--ink-soft, #666);
  border-radius: 4px;
  background: transparent;
  color: var(--ink, #2a2520);
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
  transition: all 0.2s;
}

.aurora-pb-q-btn:hover,
.aurora-pb-q-btn.active {
  border-color: var(--accent, #a8311b);
  color: var(--accent, #a8311b);
}

.aurora-pb-q-tag {
  font-size: 10px;
  padding: 1px 4px;
  border-radius: 2px;
  background: var(--accent, #a8311b);
  color: var(--paper, #f1ead8);
  line-height: 1;
}

.aurora-pb-q-menu {
  position: absolute;
  bottom: 100%;
  right: 0;
  margin-bottom: 8px;
  background: var(--paper, #f1ead8);
  border: 1px solid var(--ink-soft, #666);
  border-radius: 8px;
  padding: 6px 0;
  min-width: 140px;
  box-shadow: 0 8px 24px rgba(40,28,12,0.2);
  z-index: 1001;
}

.aurora-pb-q-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  cursor: pointer;
  transition: background 0.15s;
  font-size: 13px;
}

.aurora-pb-q-option:hover {
  background: rgba(102,102,102,0.1);
}

.aurora-pb-q-option.active {
  color: var(--accent, #a8311b);
  font-weight: 600;
}

.aurora-pb-q-current {
  font-size: 11px;
  color: var(--ink-soft, #666);
  margin-left: 8px;
}

.aurora-pb-icon {
  width: 26px;
  height: 26px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  cursor: pointer;
  color: var(--ink-soft, #666);
  position: relative;
  background: transparent;
  border: none;
  transition: color 0.2s;
}

.aurora-pb-icon:hover {
  color: var(--ink, #2a2520);
}

.aurora-pb-icon svg {
  width: 16px;
  height: 16px;
}

.aurora-pb-lyric {
  width: 30px;
  height: 24px;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 4px;
  background: transparent;
  font-weight: 600;
  font-size: 13px;
  color: var(--ink, #2a2520);
}

.aurora-pb-volume {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 90px;
}

.aurora-pb-vol-bar {
  position: relative;
  flex: 1;
  height: 12px;
  display: flex;
  align-items: center;
  cursor: pointer;
}

.aurora-pb-vol-bar::before {
  content: "";
  position: absolute;
  left: 0;
  right: 0;
  top: 50%;
  height: 1px;
  background: var(--border-subtle, rgba(34,27,18,0.18));
}

.aurora-pb-vol-fill {
  position: absolute;
  left: 0;
  top: 50%;
  height: 2px;
  background: var(--ink, #2a2520);
  transform: translateY(-1px);
}

.mode-toast {
  position: absolute;
  top: -48px;
  left: 50%;
  transform: translateX(-50%);
  background: var(--ink, #2a2520);
  color: var(--paper, #f1ead8);
  padding: 8px 16px;
  border-radius: 20px;
  font-size: 13px;
  font-weight: 500;
  box-shadow: 0 4px 12px rgba(40,28,12,0.25);
  pointer-events: none;
  z-index: 1000;
}

.toast-fade-enter-active,
.toast-fade-leave-active {
  transition: all 0.3s var(--ease-spa, ease);
}

.toast-fade-enter-from,
.toast-fade-leave-to {
  opacity: 0;
  transform: translate(-50%, 10px);
}

.menu-fade-enter-active,
.menu-fade-leave-active {
  transition: all 0.2s var(--ease-spa, ease);
}

.menu-fade-enter-from,
.menu-fade-leave-to {
  opacity: 0;
  transform: translateY(4px);
}
</style>

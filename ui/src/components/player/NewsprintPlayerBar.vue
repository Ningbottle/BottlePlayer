<script setup lang="ts">
import { computed } from 'vue';
import type { PlayerController } from './usePlayerControls';
import PlayerProgress from './PlayerProgress.vue';

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
  el.style.transform = 'translateY(1px)';
  el.style.boxShadow = '0 1px 2px rgba(0,0,0,0.15)';
}

function onRelease(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  el.style.transform = '';
  el.style.boxShadow = '';
}
</script>

<template>
  <div class="np-pb" @click="c.closeQualityMenu">
    <!-- Mode Toast -->
    <transition name="toast-fade">
      <div v-if="c.toastMsg" class="mode-toast np-pb-toast">
        {{ c.toastMsg }}
      </div>
    </transition>

    <!-- Favorite toast -->
    <transition name="toast-fade">
      <div v-if="c.favoriteMsg" class="mode-toast np-pb-toast" style="top: -48px;">
        {{ c.favoriteMsg }}
      </div>
    </transition>

    <!-- Left: cover + show name with numbering -->
    <div class="np-pb-meta" @click="c.toggleLyricView" style="cursor: pointer;" title="点击查看歌词">
      <div class="np-pb-cover">
        <img :src="c.coverUrl" alt="cover" />
      </div>

      <div class="np-pb-info">
        <span class="np-pb-num">No. {{ String(c.currentTime > 0 ? 1 : 0).padStart(2, '0') }}</span>
        <template v-if="c.currentTrack">
          <b>{{ c.currentTrack.SongName }}</b>
          <span>{{ c.currentTrack.SingerName }}</span>
        </template>
        <template v-else>
          <b>未播放歌曲</b>
          <span>- -</span>
        </template>
      </div>

      <span v-if="c.errorMsg" class="np-pb-status" style="color: var(--accent);">
        {{ c.errorMsg }}
      </span>
      <span v-else-if="c.vipRequired" class="np-pb-status" style="color: var(--accent);">
        ⚠️ VIP · 试听
      </span>
      <span v-else-if="c.isPreview" class="np-pb-status">
        ⚠️ 试听
      </span>

      <button
        v-if="c.currentTrack"
        class="np-pb-fav"
        aria-label="favorite"
        title="收藏"
        @click.stop="c.handleFavorite"
      >
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="14" height="14">
          <path d="M12 2l2.39 6.96H22l-6 4.62L18.18 21 12 16.77 5.82 21 8 13.58 2 8.96h7.61z"/>
        </svg>
      </button>
    </div>

    <!-- Transport: block-style buttons, no capsules -->
    <div class="np-pb-transport">
      <button
        class="np-pb-btn"
        :class="{ active: c.loopMode === 'random' }"
        aria-label="shuffle"
        title="随机播放"
        @click="c.toggleShuffle"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <span class="np-pb-btn-label">SHF</span>
      </button>

      <button
        class="np-pb-btn"
        aria-label="prev"
        @click="c.prev"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <svg viewBox="0 0 24 24" fill="currentColor" width="16" height="16">
          <polygon points="6,5 6,19 8,19 8,13 19,19 19,5 8,11 8,5"/>
        </svg>
      </button>

      <button
        class="np-pb-btn np-pb-play"
        :aria-label="c.showPauseIcon ? 'pause' : 'play'"
        :title="c.isLoading ? '取消加载' : (c.isPlaying ? '暂停' : '播放')"
        @click="c.togglePlay"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <span class="np-pb-btn-label">{{ c.showPauseIcon ? 'PAUSE' : 'PLAY' }}</span>
      </button>

      <button
        class="np-pb-btn"
        aria-label="next"
        @click="c.next"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <svg viewBox="0 0 24 24" fill="currentColor" width="16" height="16">
          <polygon points="5,5 16,11 16,5 18,5 18,19 16,19 16,13 5,19"/>
        </svg>
      </button>

      <button
        class="np-pb-btn"
        :class="{ active: c.loopMode === 'single' }"
        aria-label="repeat"
        title="单曲循环"
        @click="c.toggleRepeat"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <span class="np-pb-btn-label">{{ c.loopMode === 'single' ? '1×' : 'RPT' }}</span>
      </button>
    </div>

    <!-- Progress: straight line, no thumb rounding -->
    <div class="np-pb-progress">
      <PlayerProgress
        :current-time="c.currentTime"
        :duration="c.duration"
        @seek="c.seek"
      />
    </div>

    <!-- Auxiliary: quality, queue, lyric, volume -->
    <div class="np-pb-aux">
      <div class="np-pb-quality" @click.stop>
        <button
          class="np-pb-q-btn"
          :class="{ active: c.showQualityMenu }"
          @click="c.showQualityMenu = !c.showQualityMenu"
          title="音质选择"
        >
          {{ c.getQualityLabel(c.quality) }}
        </button>

        <transition name="menu-fade">
          <div v-if="c.showQualityMenu" class="np-pb-q-menu" @click="c.closeQualityMenu">
            <div
              v-for="q in c.qualityOptions"
              :key="q"
              class="np-pb-q-option"
              :class="{ active: c.isCurrentQuality(q) }"
              @click="c.handleSelectQuality(q)"
            >
              <span>{{ c.getQualityLabel(q) }}</span>
              <span v-if="c.isCurrentQuality(q)" class="np-pb-q-current">·</span>
            </div>
          </div>
        </transition>
      </div>

      <button
        class="np-pb-icon"
        aria-label="queue"
        @click="emit('toggle-queue')"
        title="播放队列"
      >
        <span class="np-pb-btn-label">Q</span>
      </button>

      <button
        class="np-pb-icon np-pb-lyric"
        aria-label="lyric"
        @click="c.toggleLyricView"
      >
        词
      </button>

      <div class="np-pb-volume">
        <span class="np-pb-vol-label">VOL</span>
        <div class="np-pb-vol-bar" @click="handleVolumeClick">
          <div class="np-pb-vol-fill" :style="{ width: c.volumePercent + '%' }"></div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.np-pb {
  position: relative;
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 10px 20px;
  height: 88px;
  box-sizing: border-box;
  font-family: 'Noto Serif SC', serif;
}

.np-pb-meta {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
  flex: 0 0 280px;
  position: relative;
}

.np-pb-cover {
  width: 52px;
  height: 52px;
  border-radius: 2px;
  border: 1px solid var(--border-subtle, #ccc);
  overflow: hidden;
  flex: none;
  background: var(--surface-1, #f1ead8);
}

.np-pb-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition: opacity 0.15s var(--ease-spa, ease);
}

.np-pb-info {
  display: flex;
  flex-direction: column;
  min-width: 0;
  gap: 1px;
}

.np-pb-num {
  font-family: 'EB Garamond', serif;
  font-size: 10px;
  font-weight: 400;
  letter-spacing: 0.1em;
  color: var(--ink-mute, #8a7e6a);
  text-transform: uppercase;
}

.np-pb-info b {
  font-size: 14px;
  font-weight: 700;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  color: var(--ink, #2a2520);
}

.np-pb-info span {
  font-family: 'EB Garamond', serif;
  font-style: italic;
  font-size: 12px;
  color: var(--ink-soft, #666);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.np-pb-status {
  font-size: 11px;
  margin-left: 8px;
  color: var(--ink-soft, #666);
  white-space: nowrap;
}

.np-pb-fav {
  background: none;
  border: none;
  padding: 4px;
  margin-left: 8px;
  cursor: pointer;
  color: var(--ink-mute, #8a7e6a);
  transition: color 0.2s;
  display: inline-flex;
  align-items: center;
}

.np-pb-fav:hover {
  color: var(--accent, #a8311b);
}

.np-pb-transport {
  display: flex;
  align-items: center;
  gap: 4px;
  flex: none;
}

.np-pb-btn {
  display: grid;
  place-items: center;
  min-width: 36px;
  height: 28px;
  padding: 0 8px;
  cursor: pointer;
  color: var(--ink-soft, #666);
  background: transparent;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  transition: transform 0.1s power2.out, box-shadow 0.1s power2.out, color 0.2s;
  font-family: 'Noto Serif SC', serif;
}

.np-pb-btn:hover {
  color: var(--ink, #2a2520);
}

.np-pb-btn.active {
  color: var(--accent, #a8311b);
  border-color: var(--accent, #a8311b);
}

.np-pb-btn-label {
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.08em;
}

.np-pb-play {
  min-width: 56px;
  height: 32px;
  background: var(--ink, #2a2520);
  color: var(--paper, #f1ead8);
  border-color: var(--ink, #2a2520);
}

.np-pb-play:hover {
  color: var(--paper, #f1ead8);
}

.np-pb-progress {
  flex: 1;
  min-width: 0;
  display: flex;
  align-items: center;
}

.np-pb-aux {
  display: flex;
  align-items: center;
  gap: 8px;
  flex: 0 0 auto;
  color: var(--ink-soft, #666);
}

.np-pb-quality {
  position: relative;
}

.np-pb-q-btn {
  font-family: 'Noto Serif SC', serif;
  font-size: 12px;
  font-weight: 700;
  padding: 4px 8px;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  background: transparent;
  color: var(--ink, #2a2520);
  cursor: pointer;
  transition: all 0.2s;
}

.np-pb-q-btn:hover,
.np-pb-q-btn.active {
  border-color: var(--accent, #a8311b);
  color: var(--accent, #a8311b);
}

.np-pb-q-menu {
  position: absolute;
  bottom: 100%;
  right: 0;
  margin-bottom: 8px;
  background: var(--paper, #f1ead8);
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  padding: 4px 0;
  min-width: 120px;
  box-shadow: 0 4px 12px rgba(0,0,0,0.15);
  z-index: 1001;
}

.np-pb-q-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 12px;
  cursor: pointer;
  transition: background 0.15s;
  font-size: 12px;
  font-weight: 600;
}

.np-pb-q-option:hover {
  background: rgba(0,0,0,0.06);
}

.np-pb-q-option.active {
  color: var(--accent, #a8311b);
}

.np-pb-q-current {
  font-size: 14px;
  color: var(--accent, #a8311b);
}

.np-pb-icon {
  display: grid;
  place-items: center;
  min-width: 28px;
  height: 28px;
  padding: 0 6px;
  cursor: pointer;
  color: var(--ink-soft, #666);
  background: transparent;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  transition: transform 0.1s power2.out, box-shadow 0.1s power2.out, color 0.2s;
  font-family: 'Noto Serif SC', serif;
}

.np-pb-icon:hover {
  color: var(--ink, #2a2520);
}

.np-pb-lyric {
  font-weight: 700;
  font-size: 13px;
}

.np-pb-volume {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 90px;
}

.np-pb-vol-label {
  font-family: 'EB Garamond', serif;
  font-size: 10px;
  letter-spacing: 0.1em;
  color: var(--ink-mute, #8a7e6a);
}

.np-pb-vol-bar {
  position: relative;
  flex: 1;
  height: 12px;
  display: flex;
  align-items: center;
  cursor: pointer;
}

.np-pb-vol-bar::before {
  content: "";
  position: absolute;
  left: 0;
  right: 0;
  top: 50%;
  height: 1px;
  background: var(--border-subtle, rgba(34,27,18,0.18));
}

.np-pb-vol-fill {
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
  border-radius: 2px;
  font-size: 13px;
  font-weight: 500;
  box-shadow: 0 4px 12px rgba(0,0,0,0.2);
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

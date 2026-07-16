<script setup lang="ts">
import { computed } from 'vue';
import {
  Disc3,
  FileText,
  Heart,
  ListMusic,
  Maximize2,
  Pause,
  Play,
  Repeat2,
  Shuffle,
  SkipBack,
  SkipForward,
  Volume2,
} from '@lucide/vue';
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
}

function onRelease(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  el.style.transform = '';
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
    <div class="np-pb-meta">
      <div class="np-pb-cover-stack">
        <button
          type="button"
          class="np-pb-cover-btn"
          data-test="np-pb-cover-immersion"
          aria-label="打开歌词"
          title="打开歌词"
          :disabled="!c.currentTrack"
          @click.stop="c.openLyricView"
        >
          <div class="np-pb-cover">
            <img v-if="c.coverUrl" :src="c.coverUrl" alt="cover" />
            <Disc3
              v-else
              class="np-pb-cover-placeholder"
              data-test="player-cover-placeholder"
              data-icon-family="lucide"
              :size="25"
              :stroke-width="1.35"
              aria-hidden="true"
            />
          </div>
        </button>
        <button
          type="button"
          class="np-pb-enter-fullscreen"
          data-test="np-pb-enter-fullscreen"
          aria-label="进入全屏歌词"
          title="进入全屏歌词"
          :disabled="!c.currentTrack"
          @click.stop="c.openLyricImmersion"
        >
          <Maximize2 :size="12" :stroke-width="1.75" aria-hidden="true" />
        </button>
      </div>

      <button
        class="np-pb-info-btn"
        type="button"
        aria-label="查看歌曲歌词"
        title="点击查看歌词"
        :disabled="!c.currentTrack"
        @click.stop="c.openLyricView"
      >
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
      </button>

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
        type="button"
        aria-label="收藏"
        title="收藏"
        @click.stop="c.handleFavorite"
      >
        <Heart :size="14" :stroke-width="1.75" aria-hidden="true" />
      </button>
    </div>

    <!-- Transport: block-style buttons, no capsules — hidden when no track -->
    <div
      v-if="c.currentTrack"
      class="np-pb-transport"
      data-test="newsprint-player-transport"
    >
      <button
        type="button"
        class="np-pb-btn"
        :class="{ active: c.loopMode === 'random' }"
        aria-label="随机"
        :aria-pressed="c.loopMode === 'random'"
        title="随机播放"
        @click="c.toggleShuffle"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <Shuffle :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="np-pb-btn"
        aria-label="上一首"
        title="上一首"
        @click="c.prev"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <SkipBack :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="np-pb-btn np-pb-play"
        :aria-label="c.showPauseIcon ? '暂停' : '播放'"
        :title="c.isLoading ? '取消加载' : (c.isPlaying ? '暂停' : '播放')"
        @click="c.togglePlay"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <Pause v-if="c.showPauseIcon" :size="16" :stroke-width="1.75" aria-hidden="true" />
        <Play v-else :size="16" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="np-pb-btn"
        aria-label="下一首"
        title="下一首"
        @click="c.next"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <SkipForward :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="np-pb-btn"
        :class="{ active: c.loopMode === 'single' }"
        aria-label="循环"
        :aria-pressed="c.loopMode === 'single'"
        title="单曲循环"
        @click="c.toggleRepeat"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <Repeat2 :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>
    </div>
    <div
      v-else
      class="np-pb-empty-console"
      data-test="newsprint-player-empty-console"
    >
      选择曲目后显示播放控制
    </div>

    <!-- Progress: straight line, no thumb rounding -->
    <div v-if="c.currentTrack" class="np-pb-progress">
      <PlayerProgress
        :current-time="c.currentTime"
        :duration="c.duration"
        @seek="c.seek"
      />
    </div>

    <!-- Auxiliary: quality, queue, lyric, volume -->
    <div class="np-pb-aux">
      <div v-if="c.currentTrack" class="np-pb-quality" data-test="newsprint-player-quality" @click.stop>
        <button
          type="button"
          class="np-pb-q-btn"
          :class="{ active: c.showQualityMenu }"
          aria-label="选择音质"
          :aria-expanded="c.showQualityMenu"
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
        type="button"
        class="np-pb-icon"
        aria-label="队列"
        @click="emit('toggle-queue')"
        title="播放队列"
      >
        <ListMusic :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="np-pb-icon np-pb-lyric"
        :class="{ active: c.isLyricView }"
        aria-label="歌词"
        title="歌词"
        @click="c.toggleLyricView"
      >
        <FileText :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>

      <div class="np-pb-volume">
        <Volume2 class="np-pb-vol-icon" :size="14" :stroke-width="1.75" aria-hidden="true" />
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
  padding: 6px 16px;
  height: 72px;
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
  display: grid;
  place-items: center;
}

.np-pb-cover-stack {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  flex: none;
}

.np-pb-cover-btn,
.np-pb-info-btn,
.np-pb-enter-fullscreen {
  appearance: none;
  border: 0;
  padding: 0;
  margin: 0;
  color: inherit;
  background: transparent;
  cursor: pointer;
  text-align: left;
}

.np-pb-cover-btn:focus-visible,
.np-pb-info-btn:focus-visible,
.np-pb-enter-fullscreen:focus-visible {
  outline: 1px solid var(--accent, #a8311b);
  outline-offset: 2px;
}

.np-pb-cover-btn:disabled,
.np-pb-enter-fullscreen:disabled {
  cursor: default;
  opacity: 0.45;
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

.np-pb-info-btn {
  min-width: 0;
  flex: 1 1 auto;
}

.np-pb-enter-fullscreen {
  color: var(--ink-mute, #8a7e6a);
  width: 14px;
  height: 12px;
  display: grid;
  place-items: center;
  flex: 0 0 auto;
  line-height: 0;
  white-space: nowrap;
  transition: color 0.15s ease;
}

.np-pb-enter-fullscreen:hover:not(:disabled),
.np-pb-enter-fullscreen:focus-visible {
  color: var(--ink, #2a2520);
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
  justify-content: center;
  line-height: 0;
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

.np-pb-empty-console {
  flex: none;
  min-width: 160px;
  padding: 6px 10px;
  border: 1px dashed var(--border-subtle, #ccc);
  color: var(--ink-mute, #8a7e6a);
  font-size: 11px;
  font-family: 'Noto Serif SC', Georgia, serif;
  letter-spacing: 0.04em;
}

.np-pb-btn {
  display: grid;
  place-items: center;
  width: 32px;
  min-width: 32px;
  height: 28px;
  padding: 0;
  cursor: pointer;
  color: var(--ink-soft, #666);
  background: transparent;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  transition: transform 0.1s ease-out, color 0.2s;
  font-family: 'Noto Serif SC', serif;
  line-height: 0;
}

.np-pb-btn:hover {
  color: var(--ink, #2a2520);
}

.np-pb-btn:focus-visible,
.np-pb-fav:focus-visible,
.np-pb-icon:focus-visible,
.np-pb-lyric:focus-visible,
.np-pb-q-btn:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 3px;
}

.np-pb-btn.active {
  color: var(--accent, #a8311b);
  border-color: var(--accent, #a8311b);
}

.np-pb-play {
  width: 34px;
  min-width: 34px;
  height: 32px;
  background: transparent;
  color: var(--ink, #2a2520);
  border-color: color-mix(in srgb, var(--ink, #2a2520) 52%, transparent);
}

.np-pb-cover-placeholder {
  color: var(--ink-soft, #756f66);
  opacity: 0.78;
}

.np-pb-play:hover {
  color: var(--accent, #a8311b);
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
  width: 28px;
  min-width: 28px;
  height: 28px;
  padding: 0;
  cursor: pointer;
  color: var(--ink-soft, #666);
  background: transparent;
  border: 1px solid var(--border-subtle, #ccc);
  border-radius: 2px;
  transition: transform 0.1s ease-out, color 0.2s;
  font-family: 'Noto Serif SC', serif;
  line-height: 0;
}

.np-pb-icon:not(.np-pb-lyric) {
  min-width: 28px;
}

.np-pb-icon:hover {
  color: var(--ink, #2a2520);
}

.np-pb-lyric {
  font-size: 0;
}

.np-pb-volume {
  display: flex;
  align-items: center;
  gap: 6px;
  width: 90px;
}

.np-pb-vol-icon {
  flex: none;
  color: var(--ink-mute, #8a7e6a);
}

@media (prefers-reduced-motion: reduce) {
  .np-pb-btn,
  .np-pb-fav,
  .np-pb-icon,
  .np-pb-enter-fullscreen {
    transition: none;
  }

  .np-pb-btn:active,
  .np-pb-fav:active,
  .np-pb-icon:active,
  .np-pb-enter-fullscreen:active {
    transform: none !important;
  }
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

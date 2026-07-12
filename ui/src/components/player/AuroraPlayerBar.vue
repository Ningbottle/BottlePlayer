<script setup lang="ts">
import { computed } from 'vue';
import type { PlayerController } from './usePlayerControls';
import PlayerProgress from './PlayerProgress.vue';
import AuroraDockParticles from './AuroraDockParticles.vue';
import { pressBounceDown, pressBounceUp } from '../../api/motion';

const props = defineProps<{
  controller: PlayerController;
}>();

const emit = defineEmits<{
  (e: 'toggle-queue'): void;
}>();

const c = computed(() => props.controller);

/** Design-target style quality chip (e.g. 无损 · 96kHz when available). */
const qualityChip = computed(() => {
  const label = c.value.getQualityLabel(c.value.quality);
  const q = String(c.value.quality || '').toLowerCase();
  if (q.includes('flac') || q.includes('hires') || q.includes('master') || label.includes('无损') || label.includes('Hi')) {
    return `${label}`;
  }
  return label;
});

function handleVolumeClick(e: MouseEvent) {
  const barEl = e.currentTarget as HTMLElement;
  const rect = barEl.getBoundingClientRect();
  const clickX = e.clientX - rect.left;
  const pct = Math.max(0, Math.min(1, clickX / rect.width));
  c.value.setVolume(pct);
}

/** Q-bounce: squash → elastic spring (elastic.out). */
function onPress(e: MouseEvent) {
  const el = e.currentTarget;
  if (el instanceof Element) pressBounceDown(el);
}

function onRelease(e: MouseEvent) {
  const el = e.currentTarget;
  if (el instanceof Element) pressBounceUp(el);
}
</script>

<template>
  <footer class="aurora-pb" @click="c.closeQualityMenu">
    <AuroraDockParticles
      class="aurora-pb-particles"
      :is-playing="!!c.currentTrack && c.isPlaying"
      :progress="c.currentTrack ? c.progressPercent / 100 : 0"
    />

    <transition name="toast-fade">
      <div v-if="c.toastMsg" class="mode-toast aurora-pb-toast">
        {{ c.toastMsg }}
      </div>
    </transition>

    <transition name="toast-fade">
      <div v-if="c.favoriteMsg" class="mode-toast aurora-pb-toast aurora-pb-toast-fav">
        {{ c.favoriteMsg }}
      </div>
    </transition>

    <!-- Left: cover + title/artist + star -->
    <div class="aurora-pb-left">
      <button
        type="button"
        class="aurora-pb-cover-btn"
        data-test="aurora-pb-cover-immersion"
        title="打开全屏歌词"
        :disabled="!c.currentTrack"
        @click.stop="c.openLyricImmersion"
      >
        <div class="aurora-pb-cover">
          <img :src="c.coverUrl" alt="cover" />
        </div>
      </button>

      <button
        type="button"
        class="aurora-pb-info-btn"
        title="点击查看歌词"
        @click="c.toggleLyricView"
      >
        <template v-if="c.currentTrack">
          <b>{{ c.currentTrack.SongName }}</b>
          <span>{{ c.currentTrack.SingerName }}</span>
        </template>
        <template v-else>
          <b>未播放歌曲</b>
          <span>- -</span>
        </template>
      </button>

      <span v-if="c.errorMsg" class="aurora-pb-status">{{ c.errorMsg }}</span>
      <span v-else-if="c.vipRequired" class="aurora-pb-status">VIP 试听</span>
      <span v-else-if="c.isPreview" class="aurora-pb-status">试听</span>

      <button
        type="button"
        class="aurora-pb-fav"
        :class="{ 'is-disabled': !c.currentTrack }"
        aria-label="favorite"
        title="收藏"
        :disabled="!c.currentTrack"
        @click.stop="c.handleFavorite"
      >
        <!-- outline star matching design -->
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" width="18" height="18">
          <path d="M12 3.2l2.35 4.76 5.25.76-3.8 3.7.9 5.24L12 15.9l-4.7 2.46.9-5.24-3.8-3.7 5.25-.76L12 3.2z"/>
        </svg>
      </button>
    </div>

    <!-- Center: transport only when a track is loaded -->
    <div class="aurora-pb-center" data-test="aurora-player-console">
      <!-- Single integrated strip (same glass family as dock — no dark island) -->
      <div
        v-if="c.currentTrack"
        class="aurora-pb-transport"
        role="group"
        aria-label="播放控制"
        data-test="aurora-player-transport"
      >
        <button
          type="button"
          class="aurora-pb-btn"
          :class="{ 'is-active': c.loopMode === 'random' }"
          aria-label="shuffle"
          title="随机播放"
          @click="c.toggleShuffle"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
            <path d="M16 3h5v5M4 20l17-17M21 16v5h-5M15 15l6 6M4 4l5 5"/>
          </svg>
        </button>

        <button
          type="button"
          class="aurora-pb-btn"
          aria-label="prev"
          title="上一首"
          @click="c.prev"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="currentColor">
            <path d="M6 6h2.2v12H6V6zm3.4 6 9.6 6.2V5.8L9.4 12z"/>
          </svg>
        </button>

        <button
          type="button"
          class="aurora-pb-btn aurora-pb-play"
          :aria-label="c.showPauseIcon ? 'pause' : 'play'"
          :title="c.isLoading ? '取消加载' : (c.isPlaying ? '暂停' : '播放')"
          @click="c.togglePlay"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg v-if="c.showPauseIcon" viewBox="0 0 24 24" fill="currentColor">
            <rect x="6" y="4" width="4" height="16" rx="1.5" />
            <rect x="14" y="4" width="4" height="16" rx="1.5" />
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="currentColor">
            <path d="M8 5.2v13.6c0 .7.75 1.12 1.34.74l10.4-6.8a.88.88 0 0 0 0-1.48l-10.4-6.8A.88.88 0 0 0 8 5.2z"/>
          </svg>
        </button>

        <button
          type="button"
          class="aurora-pb-btn"
          aria-label="next"
          title="下一首"
          @click="c.next"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg viewBox="0 0 24 24" fill="currentColor">
            <!-- triangle points right + bar on the right (skip next) -->
            <path d="M4.8 5.8v12.4L14.4 12 4.8 5.8zM15.8 6H18v12h-2.2V6z"/>
          </svg>
        </button>

        <button
          type="button"
          class="aurora-pb-btn"
          :class="{ 'is-active': c.loopMode === 'single' }"
          aria-label="repeat"
          title="单曲循环"
          @click="c.toggleRepeat"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <svg v-if="c.loopMode === 'single'" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
            <path d="M17 1l4 4-4 4M3 11V9a4 4 0 0 1 4-4h14M7 23l-4-4 4-4M21 13v2a4 4 0 0 1-4 4H3"/>
            <text x="12" y="15.5" font-size="7.5" font-weight="800" fill="currentColor" stroke="none" text-anchor="middle">1</text>
          </svg>
          <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
            <path d="M17 1l4 4-4 4M3 11V9a4 4 0 0 1 4-4h14M7 23l-4-4 4-4M21 13v2a4 4 0 0 1-4 4H3"/>
          </svg>
        </button>
      </div>
      <div
        v-else
        class="aurora-pb-empty-console"
        data-test="aurora-player-empty-console"
      >
        选择曲目后显示播放控制
      </div>

      <div v-if="c.currentTrack" class="aurora-pb-progress-wrap" data-test="aurora-player-progress">
        <PlayerProgress
          :current-time="c.currentTime"
          :duration="c.duration"
          @seek="c.seek"
        />
      </div>
    </div>

    <!-- Right: quality · lyric · volume -->
    <div class="aurora-pb-right">
      <div v-if="c.currentTrack" class="aurora-pb-quality" data-test="aurora-player-quality" @click.stop>
        <button
          type="button"
          class="aurora-pb-q-btn"
          :class="{ active: c.showQualityMenu }"
          @click="c.showQualityMenu = !c.showQualityMenu"
          title="音质选择"
        >
          <span class="aurora-pb-q-main">{{ qualityChip }}</span>
          <span class="aurora-pb-q-sub">切换</span>
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

      <button
        type="button"
        class="aurora-pb-icon aurora-pb-queue"
        aria-label="queue"
        title="播放队列"
        @click="emit('toggle-queue')"
      >
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" width="16" height="16">
          <path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>
        </svg>
      </button>

      <button
        type="button"
        class="aurora-pb-lyric"
        :class="{ active: c.isLyricView }"
        aria-label="lyric"
        title="歌词"
        @click="c.toggleLyricView"
      >
        词
      </button>

      <div class="aurora-pb-volume" title="音量">
        <svg class="aurora-pb-vol-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <path d="M11 5L6 9H2v6h4l5 4zM15.5 9.5a4 4 0 0 1 0 5M18.5 7a7.5 7.5 0 0 1 0 10"/>
        </svg>
        <div class="aurora-pb-vol-bar" @click="handleVolumeClick">
          <div class="aurora-pb-vol-fill" :style="{ width: c.volumePercent + '%' }">
            <i class="aurora-pb-vol-thumb" />
          </div>
        </div>
      </div>
    </div>
  </footer>
</template>

<style scoped>
/* ── Dock: full-width liquid capsule (design target) ── */
.aurora-pb {
  --pb-glass: color-mix(in srgb, var(--surface-elevated) 78%, #000 22%);
  position: relative;
  isolation: isolate;
  display: grid;
  grid-template-columns: minmax(180px, 0.78fr) minmax(280px, 1.5fr) minmax(180px, 0.78fr);
  gap: 6px 12px;
  align-items: center;
  min-height: 72px;
  padding: 8px 16px 10px;
  box-sizing: border-box;
  border-radius: 24px;
  border: 1px solid color-mix(in srgb, #fff 8%, transparent);
  background: var(--surface-elevated);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, var(--surface-elevated) 92%, #fff 4%) 0%,
    color-mix(in srgb, var(--surface-elevated) 78%, #000 12%) 100%
  );
  backdrop-filter: none;
  -webkit-backdrop-filter: none;
  box-shadow:
    0 18px 40px rgba(0, 0, 0, 0.38),
    0 2px 0 color-mix(in srgb, #fff 6%, transparent) inset,
    0 -1px 0 rgba(0, 0, 0, 0.25) inset;
  overflow: hidden;
}

.aurora-pb > *:not(.aurora-pb-particles) {
  position: relative;
  z-index: 1;
}

.aurora-pb-particles {
  z-index: 0;
}

:global(:root[data-mode='light']) .aurora-pb {
  border-color: color-mix(in srgb, var(--text-primary) 8%, transparent);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, #fff 94%, var(--surface-2)) 0%,
    color-mix(in srgb, var(--surface-elevated) 88%, var(--surface-2)) 100%
  );
  box-shadow:
    0 16px 36px rgba(22, 32, 29, 0.12),
    0 1px 0 rgba(255, 255, 255, 0.9) inset;
}

/* ── Left ── */
.aurora-pb-left {
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 0;
  z-index: 1;
}

.aurora-pb-cover-btn,
.aurora-pb-info-btn {
  appearance: none;
  border: 0;
  background: transparent;
  padding: 0;
  margin: 0;
  color: inherit;
  cursor: pointer;
  text-align: left;
  min-width: 0;
}

.aurora-pb-cover {
  /* Spec §7.2: 64–76px cover */
  width: 64px;
  height: 64px;
  border-radius: 12px;
  overflow: hidden;
  flex: none;
  background: var(--surface-2);
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.35);
}

.aurora-pb-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.aurora-pb-info-btn {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.aurora-pb-info-btn b {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 160px;
}

.aurora-pb-info-btn span {
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 160px;
}

.aurora-pb-status {
  font-size: 11px;
  color: var(--accent);
  white-space: nowrap;
}

.aurora-pb-fav {
  flex: none;
  width: 30px;
  height: 30px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
  transition: color 0.2s, transform 0.15s;
}

.aurora-pb-fav:hover:not(:disabled) {
  color: var(--accent);
  transform: scale(1.06);
}

.aurora-pb-fav.is-disabled,
.aurora-pb-fav:disabled {
  opacity: 0.45;
  cursor: default;
}

/*
  Spec §7: liquid dock with soft center console.
  One continuous matte plateau (not candy stickers). Play is the only filled disc.
*/
.aurora-pb-center {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  min-width: 0;
  min-height: 52px;
  padding-top: 2px;
}

.aurora-pb-empty-console {
  min-width: 200px;
  height: 36px;
  display: grid;
  place-items: center;
  padding: 0 14px;
  border-radius: 999px;
  border: 1px dashed color-mix(in srgb, var(--text-secondary, #888) 35%, transparent);
  color: var(--text-secondary, #8a8070);
  font-size: 12px;
}

/*
  Transport: one flat strip on the same glass as the dock.
  No nested dark bubble / second capsule — only the play disc carries accent.
*/
.aurora-pb-transport {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 2px;
  min-height: 44px;
  padding: 4px 6px;
  border-radius: 999px;
  /* Match dock surface — slightly lifted only */
  background: color-mix(in srgb, var(--surface-elevated) 55%, transparent);
  border: 1px solid color-mix(in srgb, #fff 6%, transparent);
  box-shadow: 0 1px 0 color-mix(in srgb, #fff 5%, transparent) inset;
  box-sizing: border-box;
}

:global(:root[data-mode='light']) .aurora-pb-transport {
  background: color-mix(in srgb, #fff 42%, transparent);
  border-color: color-mix(in srgb, var(--text-primary) 7%, transparent);
  box-shadow: 0 1px 0 rgba(255, 255, 255, 0.75) inset;
}

.aurora-pb-btn {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  cursor: pointer;
  color: color-mix(in srgb, var(--text-primary) 72%, transparent);
  background: transparent;
  border: 1px solid transparent;
  border-radius: 8px;
  box-shadow: none;
  filter: none;
  outline: none;
  /* Allow GSAP scale Q-bounce; only color/bg transition in CSS */
  transition: color 0.15s ease, background 0.15s ease, border-color 0.15s ease;
  transform-origin: center center;
  will-change: transform;
  box-sizing: border-box;
}

.aurora-pb-btn:hover {
  color: var(--text-primary);
  background: color-mix(in srgb, var(--text-primary) 7%, transparent);
  border-color: color-mix(in srgb, var(--text-primary) 10%, transparent);
}

.aurora-pb-btn:active {
  /* press depth handled by GSAP pressBounceDown */
}

.aurora-pb-btn.is-active {
  color: var(--accent);
}

.aurora-pb-btn svg {
  width: 16px;
  height: 16px;
  display: block;
}

.aurora-pb-play {
  width: 40px;
  height: 40px;
  margin: 0 2px;
  border-radius: 50%;
  background: var(--accent);
  color: #0a1410;
  box-shadow:
    0 1px 0 color-mix(in srgb, #fff 22%, transparent) inset,
    0 3px 10px color-mix(in srgb, var(--accent) 28%, transparent);
}

.aurora-pb-play:hover {
  filter: brightness(1.05);
  background: var(--accent);
  color: #0a1410;
}

.aurora-pb-play svg {
  width: 16px;
  height: 16px;
}

.aurora-pb-progress-wrap {
  width: 100%;
  max-width: 620px;
  min-width: 0;
  padding: 0 10px;
  z-index: 1;
}

/* Slim emerald progress under the bubble */
.aurora-pb-progress-wrap :deep(.progress-root) {
  gap: 10px;
}

.aurora-pb-progress-wrap :deep(.progress-time) {
  font-family: var(--font-sans, system-ui, sans-serif);
  font-style: normal;
  font-size: 11px;
  font-variant-numeric: tabular-nums;
  color: var(--text-muted);
  min-width: 34px;
}

.aurora-pb-progress-wrap :deep(.progress-track) {
  height: 14px;
}

.aurora-pb-progress-wrap :deep(.progress-track::before) {
  height: 3px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--progress-track) 85%, transparent);
}

.aurora-pb-progress-wrap :deep(.progress-fill) {
  height: 3px;
  border-radius: 999px;
  background: var(--progress-fill);
}

.aurora-pb-progress-wrap :deep(.progress-thumb) {
  width: 11px;
  height: 11px;
  border: 2px solid var(--progress-thumb-ring, var(--accent));
  background: var(--progress-thumb-fill, #fff);
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.35);
}

/* ── Right ── */
.aurora-pb-right {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 10px;
  min-width: 0;
  z-index: 1;
  color: var(--text-secondary);
}

.aurora-pb-quality {
  position: relative;
}

.aurora-pb-q-btn {
  display: inline-flex;
  align-items: baseline;
  gap: 6px;
  padding: 4px 2px;
  border: 0;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  font: inherit;
  white-space: nowrap;
}

.aurora-pb-q-main {
  font-size: 12px;
  font-weight: 500;
  color: var(--text-secondary);
}

.aurora-pb-q-sub {
  font-size: 10px;
  color: var(--text-muted);
  opacity: 0.85;
}

.aurora-pb-q-btn:hover .aurora-pb-q-main,
.aurora-pb-q-btn.active .aurora-pb-q-main {
  color: var(--accent);
}

.aurora-pb-q-menu {
  position: absolute;
  bottom: calc(100% + 10px);
  right: 0;
  min-width: 148px;
  padding: 6px 0;
  border-radius: 12px;
  border: 1px solid var(--border-subtle);
  background: var(--surface-elevated);
  box-shadow: 0 12px 28px rgba(0, 0, 0, 0.35);
  z-index: 1001;
}

.aurora-pb-q-option {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 14px;
  cursor: pointer;
  font-size: 13px;
  color: var(--text-primary);
}

.aurora-pb-q-option:hover {
  background: color-mix(in srgb, var(--text-primary) 6%, transparent);
}

.aurora-pb-q-option.active {
  color: var(--accent);
  font-weight: 600;
}

.aurora-pb-q-current {
  font-size: 11px;
  color: var(--text-muted);
}

.aurora-pb-icon {
  width: 30px;
  height: 30px;
  display: grid;
  place-items: center;
  border: 1px solid color-mix(in srgb, var(--text-primary) 12%, transparent);
  border-radius: 8px;
  background: color-mix(in srgb, var(--surface-2) 45%, transparent);
  color: var(--text-secondary);
  cursor: pointer;
  box-sizing: border-box;
  transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
}

.aurora-pb-icon:hover {
  color: var(--text-primary);
  border-color: color-mix(in srgb, var(--text-primary) 22%, transparent);
  background: color-mix(in srgb, var(--text-primary) 6%, transparent);
}

/* Design de-emphasizes queue when desktop rail is present */
.aurora-pb-queue {
  opacity: 0.55;
}

.aurora-pb-lyric {
  min-width: 30px;
  height: 26px;
  padding: 0 8px;
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--text-primary) 12%, transparent);
  background: color-mix(in srgb, var(--surface-2) 55%, transparent);
  color: var(--text-primary);
  font-size: 13px;
  font-weight: 600;
  cursor: pointer;
  line-height: 1;
}

.aurora-pb-lyric.active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
}

.aurora-pb-volume {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 108px;
  flex: none;
}

.aurora-pb-vol-icon {
  flex: none;
  color: var(--text-secondary);
}

.aurora-pb-vol-bar {
  position: relative;
  flex: 1;
  height: 16px;
  display: flex;
  align-items: center;
  cursor: pointer;
}

.aurora-pb-vol-bar::before {
  content: '';
  position: absolute;
  left: 0;
  right: 0;
  top: 50%;
  height: 3px;
  border-radius: 999px;
  background: var(--progress-track);
  transform: translateY(-50%);
}

.aurora-pb-vol-fill {
  position: absolute;
  left: 0;
  top: 50%;
  height: 3px;
  border-radius: 999px;
  background: var(--progress-fill);
  transform: translateY(-50%);
}

.aurora-pb-vol-thumb {
  position: absolute;
  right: -5px;
  top: 50%;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 0 0 1px color-mix(in srgb, var(--accent) 40%, transparent), 0 1px 3px rgba(0, 0, 0, 0.35);
  transform: translateY(-50%);
}

.mode-toast {
  position: absolute;
  top: -44px;
  left: 50%;
  transform: translateX(-50%);
  background: var(--surface-elevated);
  color: var(--text-primary);
  border: 1px solid var(--border-subtle);
  padding: 8px 16px;
  border-radius: 20px;
  font-size: 13px;
  font-weight: 500;
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.28);
  pointer-events: none;
  z-index: 1000;
}

.aurora-pb-toast-fav {
  top: -52px;
}

.toast-fade-enter-active,
.toast-fade-leave-active {
  transition: all 0.25s ease;
}

.toast-fade-enter-from,
.toast-fade-leave-to {
  opacity: 0;
  transform: translate(-50%, 8px);
}

.menu-fade-enter-active,
.menu-fade-leave-active {
  transition: all 0.18s ease;
}

.menu-fade-enter-from,
.menu-fade-leave-to {
  opacity: 0;
  transform: translateY(4px);
}

@media (max-width: 899px) {
  .aurora-pb {
    grid-template-columns: 1fr;
    gap: 8px;
    min-height: 0;
    border-radius: 28px;
    padding: 10px 12px 12px;
  }

  .aurora-pb-center {
    order: 2;
    min-height: 58px;
  }

  .aurora-pb-transport {
    margin-top: 0;
    min-width: 200px;
  }

  .aurora-pb-right {
    order: 3;
    justify-content: space-between;
  }

  .aurora-pb-queue {
    opacity: 1;
  }
}
</style>

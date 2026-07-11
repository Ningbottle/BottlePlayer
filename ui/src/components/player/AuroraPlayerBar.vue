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

/** Jelly press only on the solid play control — flat prev/next stay still. */
function onPress(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  if (!el.classList.contains('aurora-pb-play')) return;
  animateElement(el, { scale: 1 }, { scale: 0.94 }, 'controlPress');
}

function onRelease(e: MouseEvent) {
  const el = e.currentTarget as HTMLElement;
  if (!el.classList.contains('aurora-pb-play')) return;
  animateElement(el, { scale: 0.94 }, { scale: 1 }, 'controlRelease');
}
</script>

<template>
  <footer class="aurora-pb" @click="c.closeQualityMenu">
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
        title="点击查看歌词"
        @click="c.toggleLyricView"
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

    <!-- Center: raised transport bubble + progress under it -->
    <div class="aurora-pb-center" data-test="aurora-player-console">
      <div class="aurora-pb-bubble">
        <div class="aurora-pb-transport" role="group" aria-label="播放控制">
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
            @click="c.prev"
            @mousedown="onPress"
            @mouseup="onRelease"
            @mouseleave="onRelease"
          >
            <svg viewBox="0 0 24 24" fill="currentColor">
              <path d="M6 5v14h2V13.5L18 19V5L8 10.5V5H6z"/>
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
              <rect x="6" y="4" width="4" height="16" rx="1" />
              <rect x="14" y="4" width="4" height="16" rx="1" />
            </svg>
            <svg v-else viewBox="0 0 24 24" fill="currentColor">
              <path d="M8 5.5v13l11-6.5L8 5.5z"/>
            </svg>
          </button>

          <button
            type="button"
            class="aurora-pb-btn"
            aria-label="next"
            @click="c.next"
            @mousedown="onPress"
            @mouseup="onRelease"
            @mouseleave="onRelease"
          >
            <svg viewBox="0 0 24 24" fill="currentColor">
              <path d="M18 5v14h-2V13.5L6 19V5l10 5.5V5h2z"/>
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
      </div>

      <div class="aurora-pb-progress-wrap" data-test="aurora-player-progress">
        <PlayerProgress
          :current-time="c.currentTime"
          :duration="c.duration"
          @seek="c.seek"
        />
      </div>
    </div>

    <!-- Right: quality · lyric · volume (queue kept for a11y, de-emphasized when rail visible) -->
    <div class="aurora-pb-right">
      <div class="aurora-pb-quality" @click.stop>
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
  --pb-bubble: color-mix(in srgb, var(--surface-2) 70%, #000 30%);
  position: relative;
  display: grid;
  grid-template-columns: minmax(200px, 0.78fr) minmax(360px, 1.5fr) minmax(220px, 0.78fr);
  gap: 8px 16px;
  align-items: center;
  min-height: 88px;
  padding: 12px 24px 14px;
  box-sizing: border-box;
  border-radius: 48px;
  border: 1px solid color-mix(in srgb, #fff 8%, transparent);
  background: var(--surface-elevated);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, var(--surface-elevated) 92%, #fff 4%) 0%,
    color-mix(in srgb, var(--surface-elevated) 78%, #000 12%) 100%
  );
  backdrop-filter: blur(28px) saturate(160%);
  -webkit-backdrop-filter: blur(28px) saturate(160%);
  box-shadow:
    0 18px 40px rgba(0, 0, 0, 0.38),
    0 2px 0 color-mix(in srgb, #fff 6%, transparent) inset,
    0 -1px 0 rgba(0, 0, 0, 0.25) inset;
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
  width: 52px;
  height: 52px;
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

/* ── Center console ──
   Design: ONE soft elongated platform; only play is filled/raised.
   Prev/next/shuffle/repeat stay flat monoline icons (no per-button bump). */
.aurora-pb-center {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: flex-end;
  gap: 0;
  min-width: 0;
  min-height: 70px;
  padding-top: 18px;
}

/* Single liquid tray — continuous surface, not individual raised keys */
.aurora-pb-bubble {
  position: absolute;
  top: -18px;
  left: 50%;
  transform: translateX(-50%);
  z-index: 3;
  display: flex;
  align-items: center;
  justify-content: center;
  min-width: 292px;
  height: 58px;
  padding: 0 28px;
  border-radius: 999px;
  border: 1px solid color-mix(in srgb, #fff 6%, transparent);
  background: color-mix(in srgb, var(--surface-2) 82%, #000 18%);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, var(--surface-2) 86%, #fff 4%) 0%,
    color-mix(in srgb, var(--surface-2) 74%, #000 14%) 100%
  );
  box-shadow:
    0 8px 22px rgba(0, 0, 0, 0.28),
    0 1px 0 color-mix(in srgb, #fff 8%, transparent) inset;
}

:global(:root[data-mode='light']) .aurora-pb-bubble {
  border-color: color-mix(in srgb, var(--text-primary) 6%, transparent);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, #fff 96%, var(--surface-2)) 0%,
    color-mix(in srgb, var(--surface-2) 88%, #fff) 100%
  );
  box-shadow:
    0 8px 18px rgba(22, 32, 29, 0.1),
    0 1px 0 rgba(255, 255, 255, 0.9) inset;
}

.aurora-pb-transport {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 20px;
}

/* Flat secondary transport — no fill, no outer glow, no 3D */
.aurora-pb-btn {
  width: 28px;
  height: 28px;
  display: grid;
  place-items: center;
  cursor: pointer;
  color: color-mix(in srgb, var(--text-secondary) 92%, transparent);
  background: transparent !important;
  border: none !important;
  border-radius: 0;
  box-shadow: none !important;
  filter: none !important;
  transition: color 0.15s, opacity 0.15s;
}

.aurora-pb-btn:hover {
  color: var(--text-primary);
  transform: none;
}

.aurora-pb-btn.is-active {
  color: var(--accent);
}

.aurora-pb-btn svg {
  width: 18px;
  height: 18px;
  display: block;
}

/* Only play is the raised solid control */
.aurora-pb-play {
  width: 46px;
  height: 46px;
  border-radius: 50% !important;
  background: var(--accent) !important;
  color: #07120e !important;
  box-shadow:
    0 6px 16px color-mix(in srgb, var(--accent) 40%, transparent),
    0 1px 0 color-mix(in srgb, #fff 30%, transparent) inset !important;
}

.aurora-pb-play:hover {
  filter: brightness(1.07) !important;
  color: #040c09 !important;
  transform: none;
}

.aurora-pb-play svg {
  width: 18px;
  height: 18px;
}

.aurora-pb-progress-wrap {
  width: 100%;
  max-width: 640px;
  min-width: 0;
  padding: 0 12px;
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
  width: 28px;
  height: 28px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
}

.aurora-pb-icon:hover {
  color: var(--text-primary);
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
    gap: 10px;
    min-height: 0;
    border-radius: 28px;
    padding: 12px 14px 14px;
  }

  .aurora-pb-center {
    order: 2;
    padding-top: 22px;
    min-height: 78px;
  }

  .aurora-pb-bubble {
    top: -6px;
    min-width: 240px;
    height: 52px;
    padding: 0 18px;
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

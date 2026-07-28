<script setup lang="ts">
import { computed, ref, watch, onMounted, onUnmounted } from 'vue';
import {
  PhAppWindow,
  PhArrowsOutSimple,
  PhDisc,
  PhHeart,
  PhNote,
  PhPause,
  PhPlay,
  PhQueue,
  PhQuotes,
  PhRepeat,
  PhRepeatOnce,
  PhShuffle,
  PhSkipBack,
  PhSkipForward,
  PhSpeakerHigh,
} from '@phosphor-icons/vue';
import type { PlayerController } from './usePlayerControls';
import PlayerProgress from './PlayerProgress.vue';
import { pressBounceDown, pressBounceUp, attachMagnet } from '../../api/motion';
import { toggleOverlay } from '../../api/overlayWindows';

/** Overlay toggles surface failures on-screen (the windows themselves can't toast). */
async function onToggleOverlay(kind: 'island' | 'lyric'): Promise<void> {
  const result = await toggleOverlay(kind);
  if (result === 'failed') {
    c.value.toastMsg = kind === 'island' ? '灵动岛打开失败（见控制台）' : '桌面歌词打开失败（见控制台）';
    window.setTimeout(() => {
      if (c.value.toastMsg.includes('打开失败')) c.value.toastMsg = '';
    }, 2600);
  }
}
import { flyCoverToElement } from '../../api/coverFlight';

const props = defineProps<{
  controller: PlayerController;
}>();

const emit = defineEmits<{
  (e: 'toggle-queue'): void;
}>();

const c = computed(() => props.controller);

const playBtnEl = ref<HTMLElement | null>(null);
let detachMagnet: (() => void) | null = null;

onMounted(() => {
  if (playBtnEl.value) detachMagnet = attachMagnet(playBtnEl.value);
});

onUnmounted(() => {
  detachMagnet?.();
  detachMagnet = null;
});

/** Design-target style quality chip (e.g. 无损 · 96kHz when available). */
const qualityChip = computed(() => {
  const label = c.value.getQualityLabel(c.value.quality);
  const q = String(c.value.quality || '').toLowerCase();
  if (q.includes('flac') || q.includes('hires') || q.includes('master') || label.includes('无损') || label.includes('Hi')) {
    return `${label}`;
  }
  return label;
});

/** Volume knob: click or drag — pointer capture keeps drags inside the bar. */
let volumeDragging = false;

function updateVolumeFromPointer(e: PointerEvent): void {
  const barEl = e.currentTarget as HTMLElement;
  const rect = barEl.getBoundingClientRect();
  if (rect.width <= 0) return;
  const pct = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
  c.value.setVolume(pct);
}

function onVolumePointerDown(e: PointerEvent) {
  volumeDragging = true;
  (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
  updateVolumeFromPointer(e);
}

function onVolumePointerMove(e: PointerEvent) {
  if (volumeDragging) updateVolumeFromPointer(e);
}

function onVolumePointerUp(e: PointerEvent) {
  if (!volumeDragging) return;
  volumeDragging = false;
  (e.currentTarget as HTMLElement).releasePointerCapture?.(e.pointerId);
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

/** Lyric entries share a cover flight: dock cover → lyric stage cover. */
function openLyricWithFlight(open: () => void): void {
  const from = document.querySelector<HTMLElement>('.aurora-pb-cover');
  const url = c.value.coverUrl;
  open();
  if (from && url) {
    flyCoverToElement(from, '[data-test="lyric-cover"]', url, 300);
  }
}

/** One-shot ripple on the deck button whenever playback actually toggles. */
const rippleNonce = ref(0);
watch(
  () => c.value.isPlaying,
  () => {
    if (c.value.currentTrack) rippleNonce.value += 1;
  },
);

/** Heart pop only on a user-initiated favorite (not on store hydration). */
const favPop = ref(false);
let favPopTimer: number | undefined;

async function onFavoriteClick(): Promise<void> {
  const wasFavorite = c.value.isFavorite;
  await c.value.handleFavorite();
  if (!wasFavorite && c.value.isFavorite) {
    favPop.value = false;
    requestAnimationFrame(() => {
      favPop.value = true;
      window.clearTimeout(favPopTimer);
      favPopTimer = window.setTimeout(() => {
        favPop.value = false;
      }, 420);
    });
  }
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
      <div class="aurora-pb-cover-stack">
        <button
          type="button"
          class="aurora-pb-cover-btn"
          data-test="aurora-pb-cover-immersion"
          aria-label="打开歌词"
          title="打开歌词"
          :disabled="!c.currentTrack"
          @click.stop="openLyricWithFlight(c.openLyricView)"
        >
          <div class="aurora-pb-cover">
            <img v-if="c.coverUrl" :src="c.coverUrl" alt="cover" />
            <PhDisc
              v-else
              class="aurora-pb-cover-placeholder"
              data-test="player-cover-placeholder"
              data-icon-family="phosphor"
              :size="30"
              weight="duotone"
              aria-hidden="true"
            />
          </div>
        </button>
        <button
          type="button"
          class="aurora-pb-enter-fullscreen"
          data-test="aurora-pb-enter-fullscreen"
          aria-label="进入全屏歌词"
          title="进入全屏歌词"
          :disabled="!c.currentTrack"
          @click.stop="openLyricWithFlight(c.openLyricImmersion)"
        >
          <PhArrowsOutSimple :size="12" weight="bold" aria-hidden="true" />
        </button>
      </div>

      <button
        type="button"
        class="aurora-pb-info-btn"
        aria-label="查看歌曲歌词"
        title="点击查看歌词"
        :disabled="!c.currentTrack"
        @click.stop="openLyricWithFlight(c.openLyricView)"
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
        :class="{ 'is-disabled': !c.currentTrack, 'is-active': c.isFavorite, 'just-faved': favPop }"
        :aria-label="c.isFavorite ? '已收藏' : '收藏'"
        :title="c.isFavorite ? '已收藏' : '收藏'"
        :disabled="!c.currentTrack"
        @click.stop="onFavoriteClick"
      >
        <PhHeart :size="18" :weight="c.isFavorite ? 'fill' : 'regular'" aria-hidden="true" />
      </button>
    </div>

    <!-- Center: symmetric transport (prev / play / next) — loop mode lives in the right zone -->
    <div class="aurora-pb-center" data-test="aurora-player-console">
      <!-- Single integrated strip (same glass family as dock — no dark island) -->
      <div
        class="aurora-pb-transport"
        :class="{ 'is-muted': !c.currentTrack }"
        role="group"
        aria-label="播放控制"
        data-test="aurora-player-transport"
      >
        <button
          type="button"
          class="aurora-pb-btn"
          :disabled="!c.currentTrack"
          aria-label="上一首"
          title="上一首"
          @click="c.prev"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <PhSkipBack :size="16" weight="fill" aria-hidden="true" />
        </button>

        <button
          ref="playBtnEl"
          type="button"
          class="aurora-pb-btn aurora-pb-play"
          :disabled="!c.currentTrack"
          :aria-label="c.showPauseIcon ? '暂停' : '播放'"
          :title="c.isLoading ? '取消加载' : (c.isPlaying ? '暂停' : '播放')"
          @click="c.togglePlay"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <span
            v-if="rippleNonce > 0"
            :key="rippleNonce"
            class="aurora-pb-play-ripple"
            aria-hidden="true"
          />
          <PhPause v-if="c.showPauseIcon" :size="17" weight="fill" aria-hidden="true" />
          <PhPlay v-else :size="17" weight="fill" aria-hidden="true" />
        </button>

        <button
          type="button"
          class="aurora-pb-btn"
          :disabled="!c.currentTrack"
          aria-label="下一首"
          title="下一首"
          @click="c.next"
          @mousedown="onPress"
          @mouseup="onRelease"
          @mouseleave="onRelease"
        >
          <PhSkipForward :size="16" weight="fill" aria-hidden="true" />
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

    <!-- Right: loop · overlays · quality · lyric · volume -->
    <div class="aurora-pb-right">
      <button
        type="button"
        class="aurora-pb-icon aurora-pb-overlay"
        aria-label="灵动岛"
        title="灵动岛（悬浮迷你播放器）"
        data-test="aurora-overlay-island"
        @click="onToggleOverlay('island')"
      >
        <PhAppWindow :size="16" weight="regular" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="aurora-pb-icon aurora-pb-overlay-lyric"
        aria-label="桌面歌词"
        title="桌面歌词（悬浮歌词条）"
        data-test="aurora-overlay-lyric"
        @click="onToggleOverlay('lyric')"
      >
        <PhNote :size="16" weight="regular" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="aurora-pb-icon aurora-pb-loop"
        :class="{ 'is-active': c.loopMode !== 'list' }"
        :disabled="!c.currentTrack"
        :aria-label="c.loopMode === 'random' ? '随机播放' : c.loopMode === 'single' ? '单曲循环' : '列表顺序播放'"
        :aria-pressed="c.loopMode !== 'list'"
        :title="c.loopMode === 'random' ? '随机播放' : c.loopMode === 'single' ? '单曲循环' : '列表顺序播放'"
        data-test="aurora-loop-mode"
        @click="c.cycleLoopMode"
        @mousedown="onPress"
        @mouseup="onRelease"
        @mouseleave="onRelease"
      >
        <PhShuffle v-if="c.loopMode === 'random'" :size="16" weight="bold" aria-hidden="true" />
        <PhRepeatOnce v-else-if="c.loopMode === 'single'" :size="16" weight="bold" aria-hidden="true" />
        <PhRepeat v-else :size="16" weight="bold" aria-hidden="true" />
      </button>

      <div v-if="c.currentTrack" class="aurora-pb-quality" data-test="aurora-player-quality" @click.stop>
        <button
          type="button"
          class="aurora-pb-q-btn"
          :class="{ active: c.showQualityMenu }"
          aria-label="选择音质"
          :aria-expanded="c.showQualityMenu"
          @click="c.showQualityMenu = !c.showQualityMenu"
          title="音质选择"
        >
          <span class="aurora-pb-q-main">{{ qualityChip }}</span>
        </button>

        <transition name="menu-fade">
          <div
            v-if="c.showQualityMenu"
            class="aurora-pb-q-menu"
            data-test="aurora-quality-menu"
            role="listbox"
            aria-label="音质选项"
          >
            <button
              v-for="q in c.qualityOptions"
              :key="q"
              type="button"
              class="aurora-pb-q-option"
              role="option"
              :aria-selected="c.isCurrentQuality(q)"
              :class="{ active: c.isCurrentQuality(q) }"
              :data-test="`aurora-quality-option-${q}`"
              @click.stop="c.handleSelectQuality(q)"
            >
              <span>{{ c.getQualityLabel(q) }}</span>
              <span v-if="c.isCurrentQuality(q)" class="aurora-pb-q-current">当前</span>
            </button>
          </div>
        </transition>
      </div>

      <button
        type="button"
        class="aurora-pb-icon aurora-pb-queue"
        aria-label="队列"
        title="播放队列"
        @click="emit('toggle-queue')"
      >
        <PhQueue :size="16" weight="regular" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="aurora-pb-lyric"
        :class="{ active: c.isLyricView }"
        aria-label="歌词"
        title="歌词"
        @click="c.toggleLyricView"
      >
        <PhQuotes :size="15" weight="bold" aria-hidden="true" />
      </button>

      <div class="aurora-pb-volume" title="音量">
        <PhSpeakerHigh class="aurora-pb-vol-icon" :size="16" weight="regular" aria-hidden="true" />
        <div
          class="aurora-pb-vol-bar"
          @pointerdown="onVolumePointerDown"
          @pointermove="onVolumePointerMove"
          @pointerup="onVolumePointerUp"
          @pointercancel="onVolumePointerUp"
        >
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
  padding: 6px 16px 8px;
  box-sizing: border-box;
  border-radius: 16px;
  border: 1px solid color-mix(in srgb, #fff 8%, transparent);
  background: var(--surface-elevated);
  background: linear-gradient(
    180deg,
    color-mix(in srgb, var(--surface-elevated) 92%, #fff 4%) 0%,
    color-mix(in srgb, var(--surface-elevated) 78%, #000 12%) 100%
  );
  backdrop-filter: none;
  -webkit-backdrop-filter: none;
  box-shadow: 0 18px 40px rgba(0, 0, 0, 0.28);
  /* Must stay visible: quality / volume menus open upward out of the dock. */
  overflow: visible;
}

/* Only the three zones stack above the dock backdrop — toasts must stay
   absolutely positioned (in-flow toasts deform the grid on every show). */
.aurora-pb > .aurora-pb-left,
.aurora-pb > .aurora-pb-center,
.aurora-pb > .aurora-pb-right {
  position: relative;
  z-index: 1;
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

.aurora-pb-cover-stack {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  flex: none;
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

.aurora-pb-enter-fullscreen {
  appearance: none;
  border: 0;
  background: transparent;
  padding: 0;
  margin: 0;
  color: var(--text-muted, #8b9098);
  cursor: pointer;
  width: 16px;
  height: 12px;
  display: grid;
  place-items: center;
  line-height: 0;
  flex: 0 0 auto;
  transition: color 0.15s ease;
}

.aurora-pb-enter-fullscreen:hover:not(:disabled),
.aurora-pb-enter-fullscreen:focus-visible {
  color: var(--text-primary);
}

.aurora-pb-enter-fullscreen:disabled {
  cursor: default;
  opacity: 0.45;
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
  display: grid;
  place-items: center;
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

.aurora-pb-fav.is-active {
  color: #e85d6c;
}

.aurora-pb-fav.is-active :deep(svg) {
  color: #e85d6c;
  fill: currentColor;
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

/* Muted console: visible but inert without a track */
.aurora-pb-transport.is-muted { opacity: 0.55; }
.aurora-pb-transport.is-muted .aurora-pb-btn { cursor: default; }
.aurora-pb-transport.is-muted .aurora-pb-play {
  background: color-mix(in srgb, var(--accent) 30%, var(--surface-2));
  color: color-mix(in srgb, var(--text-primary) 55%, transparent);
  box-shadow: none;
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
  padding: 0;
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

.aurora-pb-btn:focus-visible,
.aurora-pb-fav:focus-visible,
.aurora-pb-icon:focus-visible,
.aurora-pb-lyric:focus-visible,
.aurora-pb-q-btn:focus-visible,
.aurora-pb-cover-btn:focus-visible,
.aurora-pb-info-btn:focus-visible,
.aurora-pb-enter-fullscreen:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 3px;
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

.aurora-pb-cover-placeholder {
  color: var(--accent);
  opacity: 0.68;
}

/* Play: the only filled object — deck button with inset depth + static indicator glow */
.aurora-pb-play {
  position: relative;
  width: 44px;
  height: 44px;
  margin: 0 2px;
  border-radius: 50%;
  background: var(--accent);
  color: #0a1410;
  box-shadow:
    0 1px 0 color-mix(in srgb, #fff 18%, transparent) inset,
    0 -2px 5px rgba(0, 0, 0, 0.22) inset,
    0 0 14px color-mix(in srgb, var(--accent) 22%, transparent);
}

/* One-shot confirmation ripple on play/pause toggle */
.aurora-pb-play-ripple {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  border: 2px solid var(--accent);
  pointer-events: none;
  animation: aurora-pb-ripple 0.55s ease-out forwards;
}

@keyframes aurora-pb-ripple {
  from { transform: scale(1); opacity: 0.7; }
  to { transform: scale(1.9); opacity: 0; }
}

@media (prefers-reduced-motion: reduce) {
  .aurora-pb-play-ripple { display: none; }
}

/* Heart pop on user-initiated favorite */
.aurora-pb-fav.just-faved svg {
  animation: aurora-fav-pop 0.36s cubic-bezier(0.34, 1.56, 0.64, 1);
}

@keyframes aurora-fav-pop {
  0% { transform: scale(0.6); }
  60% { transform: scale(1.28); }
  100% { transform: scale(1); }
}

@media (prefers-reduced-motion: reduce) {
  .aurora-pb-fav.just-faved svg { animation: none; }
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

/* Needle playhead — aurora-only deep override; PlayerProgress markup untouched */
.aurora-pb-progress-wrap :deep(.progress-thumb) {
  width: 11px;
  height: 13px;
  border: 0;
  border-radius: 2px;
  background: var(--progress-thumb-fill, #fff);
  clip-path: polygon(50% 100%, 6% 12%, 94% 12%);
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.4);
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
  align-items: center;
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
  width: 100%;
  padding: 8px 14px;
  border: 0;
  background: transparent;
  cursor: pointer;
  font: inherit;
  font-size: 13px;
  color: var(--text-primary);
  text-align: left;
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
  padding: 0;
  line-height: 0;
  transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
}

.aurora-pb-icon:hover {
  color: var(--text-primary);
  border-color: color-mix(in srgb, var(--text-primary) 22%, transparent);
  background: color-mix(in srgb, var(--text-primary) 6%, transparent);
}

/* Loop mode lives in the right zone — accent when not list-order */
.aurora-pb-loop.is-active {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
}

.aurora-pb-loop:disabled {
  opacity: 0.45;
  cursor: default;
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
  line-height: 0;
  display: grid;
  place-items: center;
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
  touch-action: none;
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

/* Volume knob: same object family as the play button */
.aurora-pb-vol-thumb {
  position: absolute;
  right: -5px;
  top: 50%;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: #fff;
  box-shadow:
    0 0 0 1px color-mix(in srgb, var(--accent) 55%, transparent),
    0 1px 3px rgba(0, 0, 0, 0.35);
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
    border-radius: 18px;
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

<script setup lang="ts">
/**
 * Minimal CSS 3D playlist shelf — fullscreen only, cover-click to open.
 * No chrome text / prev-next buttons: wheel or drag to browse, click to play.
 * Teleported to body so shell overflow never clips it.
 */
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue';
import { gsap } from 'gsap';
import type { Track } from '../../shared/music/track';
import { isReducedMotion } from '../../shared/motion/motion';

const props = defineProps<{
  open: boolean;
  tracks: Track[];
  activeHash: string | null;
  /**
   * Position of the playing track in `tracks` (the real queue index). Position
   * is the identity the shelf must follow: the same song can legitimately
   * appear twice in a queue, and when the queue grows/shrinks the hash lookup
   * below would re-anchor on the *first* match and highlight the wrong card.
   * Optional so callers without a queue index keep working.
   */
  activeIndex?: number | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'select', track: Track, index: number): void;
}>();

const rootRef = ref<HTMLElement | null>(null);
const stripRef = ref<HTMLElement | null>(null);
const focusIndex = ref(0);

const dragStartX = ref<number | null>(null);
const dragAccum = ref(0);
const dragging = ref(false);
const pointerActive = ref(false);
const hovering = ref(false);
const windowStart = ref(0);

const WINDOW_SIZE = 32;
const followPaused = computed(() => pointerActive.value || dragging.value || hovering.value);

const activeQueueIndex = computed(() => {
  // Position wins over content: the caller knows the queue index of what is
  // playing, and a duplicated track must not snap the highlight back to the
  // first copy. Hash lookup remains the fallback for callers that pass no index.
  const provided = props.activeIndex;
  if (typeof provided === 'number' && Number.isFinite(provided)) {
    const idx = Math.trunc(provided);
    if (idx >= 0 && idx < props.tracks.length) return idx;
  }
  if (!props.activeHash) return 0;
  const index = props.tracks.findIndex((track) => track.FileHash === props.activeHash);
  return index >= 0 ? index : 0;
});

/** Last valid window start — the window is a viewport, not a queue boundary. */
const maxWindowStart = computed(() => Math.max(0, props.tracks.length - WINDOW_SIZE));
const clampedWindowStart = computed(() =>
  Math.min(Math.max(0, windowStart.value), maxWindowStart.value),
);

const visibleTracks = computed(() =>
  props.tracks.slice(clampedWindowStart.value, clampedWindowStart.value + WINDOW_SIZE),
);

function desiredWindowStart(): number {
  const maxStart = Math.max(0, props.tracks.length - WINDOW_SIZE);
  return Math.min(
    Math.max(activeQueueIndex.value - Math.floor(WINDOW_SIZE / 2), 0),
    maxStart,
  );
}

function syncActiveFocus(force = false): void {
  if (!force && followPaused.value) return;

  windowStart.value = desiredWindowStart();
  // Derive the focused card from position, not from a hash scan: after a queue
  // change the active track's *position* is what stayed valid.
  const visibleIndex = activeQueueIndex.value - windowStart.value;
  focusIndex.value =
    visibleIndex >= 0 && visibleIndex < visibleTracks.value.length ? visibleIndex : 0;
}

/** Is this window-local card the one that is playing? */
function isActiveCard(visibleIndex: number): boolean {
  const globalIndex = clampedWindowStart.value + visibleIndex;
  if (typeof props.activeIndex === 'number' && Number.isFinite(props.activeIndex)) {
    return globalIndex === Math.trunc(props.activeIndex);
  }
  const track = visibleTracks.value[visibleIndex];
  return !!props.activeHash && track?.FileHash === props.activeHash;
}

/** Queue position of a window-local card — what playback must jump to. */
function globalIndexOf(visibleIndex: number): number {
  return clampedWindowStart.value + visibleIndex;
}

watch(
  () => props.open,
  async (open) => {
    if (!open) {
      dragStartX.value = null;
      pointerActive.value = false;
      dragging.value = false;
      hovering.value = false;
      return;
    }
    syncActiveFocus(true);
    await nextTick();
    playOpen();
    stripRef.value?.focus({ preventScroll: true });
  },
  { immediate: true, flush: 'post' },
);

watch(
  [() => props.activeHash, () => props.activeIndex, () => props.tracks.map((track) => track.FileHash)],
  () => syncActiveFocus(),
);

function coverOf(t: Track): string {
  return t.Image || '';
}

/** Coverflow offset from focused card. */
function cardStyle(i: number): Record<string, string | number> {
  const offset = i - focusIndex.value;
  const abs = Math.abs(offset);
  const x = offset * 148;
  const rot = Math.max(-55, Math.min(55, offset * -34));
  const scale = Math.max(0.7, 1 - abs * 0.11);
  const z = 40 - abs;
  const opacity = abs > 4 ? 0 : Math.max(0.32, 1 - abs * 0.18);
  return {
    transform: `translateX(${x}px) rotateY(${rot}deg) scale(${scale})`,
    zIndex: z,
    opacity,
  };
}

function playOpen(): void {
  const root = rootRef.value;
  const strip = stripRef.value;
  if (!root) return;
  root.style.opacity = '1';
  if (isReducedMotion()) {
    gsap.set(root, { opacity: 1 });
    return;
  }
  gsap.killTweensOf(root);
  if (strip) gsap.killTweensOf(strip);
  gsap.fromTo(root, { opacity: 0 }, { opacity: 1, duration: 0.22, ease: 'power2.out' });
  if (strip) {
    gsap.fromTo(
      strip,
      { y: 24, scale: 0.95 },
      { y: 0, scale: 1, duration: 0.42, ease: 'power3.out' },
    );
  }
}

/**
 * Move focus to a window-local position, sliding the visible window when the
 * target runs past either edge. Clamping at the window end would trap browsing
 * at item 31: from item 0 of a 40-track queue, wheel/keys/drag must reach
 * item 39 — the whole logical queue, not just the first 32 cards.
 */
function spinTo(i: number): void {
  const start = clampedWindowStart.value;
  const visibleCount = Math.min(WINDOW_SIZE, props.tracks.length - start);
  const target = Math.max(0, Math.min(props.tracks.length - 1, start + i));
  if (target < start) {
    windowStart.value = Math.max(0, target);
  } else if (target >= start + visibleCount) {
    windowStart.value = Math.min(maxWindowStart.value, target - (visibleCount - 1));
  }
  focusIndex.value = target - clampedWindowStart.value;
}

function onWheel(e: WheelEvent): void {
  if (!props.open) return;
  e.preventDefault();
  if (e.deltaY > 0 || e.deltaX > 0) spinTo(focusIndex.value + 1);
  else spinTo(focusIndex.value - 1);
}

function onPointerDown(e: PointerEvent): void {
  if (!props.open || e.button !== 0) return;
  dragStartX.value = e.clientX;
  dragAccum.value = 0;
  pointerActive.value = true;
  dragging.value = false;
}

function onPointerMove(e: PointerEvent): void {
  if (dragStartX.value == null) return;
  const dx = e.clientX - dragStartX.value;
  if (Math.abs(dx) > 8 && !dragging.value) {
    dragging.value = true;
    // Capture only an actual drag. Capturing pointerdown on the parent
    // retargets ordinary card clicks to the strip and makes cards inert.
    (e.currentTarget as HTMLElement | null)?.setPointerCapture?.(e.pointerId);
  }
  // step every ~90px of drag
  const steps = Math.trunc((dx - dragAccum.value) / -90);
  if (steps !== 0) {
    spinTo(focusIndex.value + steps);
    dragAccum.value += steps * -90;
  }
}

function onPointerUp(e: PointerEvent): void {
  if (dragStartX.value == null) return;
  try {
    (e.currentTarget as HTMLElement | null)?.releasePointerCapture?.(e.pointerId);
  } catch {
    /* ignore */
  }
  dragStartX.value = null;
  pointerActive.value = false;
  // keep dragging true briefly so click after drag is ignored
  window.setTimeout(() => {
    dragging.value = false;
    syncActiveFocus();
  }, 0);
}

function onStageEnter(): void {
  hovering.value = true;
}

function onStageLeave(): void {
  hovering.value = false;
  syncActiveFocus();
}

function onKey(e: KeyboardEvent): void {
  if (!props.open) return;
  if (!['Escape', 'ArrowRight', 'ArrowDown', 'ArrowLeft', 'ArrowUp', 'Enter'].includes(e.key)) return;
  if (e.key === 'Enter' && (e.target as Element | null)?.closest?.('.shelf-backdrop')) return;
  // The modal owns these keys before the lyric stage's fullscreen listeners.
  e.preventDefault();
  e.stopImmediatePropagation();
  if (e.key === 'Escape') {
    e.preventDefault();
    emit('close');
  } else if (e.key === 'ArrowRight' || e.key === 'ArrowDown') {
    e.preventDefault();
    spinTo(focusIndex.value + 1);
  } else if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') {
    e.preventDefault();
    spinTo(focusIndex.value - 1);
  } else if (e.key === 'Enter') {
    const t = visibleTracks.value[focusIndex.value];
    if (t) emit('select', t, globalIndexOf(focusIndex.value));
  }
}

watch(
  () => props.open,
  (open) => {
    if (open) window.addEventListener('keydown', onKey, true);
    else window.removeEventListener('keydown', onKey, true);
  },
  { immediate: true },
);

onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKey, true);
  if (rootRef.value) gsap.killTweensOf(rootRef.value);
  if (stripRef.value) gsap.killTweensOf(stripRef.value);
});

function onSelect(t: Track, visibleIndex: number): void {
  if (dragging.value) return;
  emit('select', t, globalIndexOf(visibleIndex));
}
</script>

<template>
  <Teleport to="body">
    <div
      v-if="open"
      ref="rootRef"
      class="shelf-root"
      data-test="aurora-playlist-shelf"
      role="dialog"
      aria-modal="true"
      aria-label="播放队列"
    >
      <button
        type="button"
        class="shelf-backdrop"
        aria-label="关闭"
        data-test="shelf-backdrop"
        @click="emit('close')"
      />

      <div
        class="shelf-stage"
        data-test="shelf-stage"
        ref="stripRef"
        tabindex="-1"
        @wheel.prevent="onWheel"
        @mouseenter="onStageEnter"
        @mouseleave="onStageLeave"
        @pointerdown="onPointerDown"
        @pointermove="onPointerMove"
        @pointerup="onPointerUp"
        @pointercancel="onPointerUp"
      >
        <div class="shelf-floor" aria-hidden="true" />
        <div v-if="visibleTracks.length" class="shelf-track">
          <button
            v-for="(t, i) in visibleTracks"
            :key="t.FileHash || i"
            type="button"
            class="shelf-card"
            :class="{
              'is-focus': i === focusIndex,
              'is-active': isActiveCard(i),
            }"
            :style="cardStyle(i)"
            :data-test="`shelf-card-${i}`"
            :aria-label="t.SongName || '曲目'"
            :tabindex="i === focusIndex ? 0 : -1"
            @click="onSelect(t, i)"
          >
            <div class="shelf-card-face">
              <img v-if="coverOf(t)" :src="coverOf(t)" alt="" />
              <div v-else class="shelf-card-ph" aria-hidden="true" />
            </div>
          </button>
        </div>
        <p
          v-else
          class="shelf-empty"
          data-test="shelf-empty"
          role="status"
          aria-label="播放队列为空"
        >播放队列为空</p>
      </div>
    </div>
  </Teleport>
</template>

<style scoped>
.shelf-root {
  position: fixed;
  inset: 0;
  z-index: 12000;
  display: grid;
  place-items: center;
  opacity: 1;
  pointer-events: auto;
  touch-action: none;
}

.shelf-backdrop {
  position: absolute;
  inset: 0;
  border: 0;
  padding: 0;
  cursor: pointer;
  background: color-mix(in srgb, #020406 72%, transparent);
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
}

/* Full-viewport stage — no panel chrome, no text */
.shelf-stage {
  position: relative;
  z-index: 1;
  width: min(1100px, 100vw);
  height: min(420px, 70vh);
  perspective: 1200px;
  overflow: visible;
  cursor: grab;
  pointer-events: auto;
}

.shelf-stage:active {
  cursor: grabbing;
}

.shelf-floor {
  position: absolute;
  left: 10%;
  right: 10%;
  bottom: 6%;
  height: 40%;
  background: radial-gradient(
    ellipse at 50% 0%,
    color-mix(in srgb, var(--accent) 20%, transparent),
    transparent 72%
  );
  pointer-events: none;
}

.shelf-empty {
  position: absolute;
  inset: 0;
  z-index: 2;
  display: grid;
  place-items: center;
  margin: 0;
  padding: 24px;
  color: var(--text-primary, #f2f5f2);
  font-size: clamp(1rem, 2.4vw, 1.35rem);
  font-weight: 600;
  line-height: 1.6;
  text-align: center;
  text-shadow: 0 1px 12px rgba(0, 0, 0, 0.6);
  pointer-events: none;
}

.shelf-track {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  transform-style: preserve-3d;
  pointer-events: none;
}

.shelf-card {
  position: absolute;
  left: 50%;
  top: 50%;
  width: min(200px, 36vw);
  margin-left: calc(min(200px, 36vw) / -2);
  margin-top: calc(min(200px, 36vw) / -2);
  border: 0;
  padding: 0;
  background: transparent;
  cursor: pointer;
  transform-style: preserve-3d;
  transition:
    transform 0.42s cubic-bezier(0.22, 1, 0.36, 1),
    opacity 0.32s ease,
    filter 0.32s ease;
  filter: brightness(0.78);
  pointer-events: auto;
}

.shelf-card.is-focus {
  filter: brightness(1.08);
}

.shelf-card.is-active .shelf-card-face {
  outline: 2px solid color-mix(in srgb, var(--accent) 80%, transparent);
  outline-offset: 3px;
}

.shelf-card-face {
  border-radius: 14px;
  overflow: hidden;
  background: var(--surface-2, #141a1b);
  box-shadow:
    0 22px 44px rgba(0, 0, 0, 0.55),
    0 0 0 1px color-mix(in srgb, #fff 10%, transparent);
  aspect-ratio: 1;
}

.shelf-card-face img {
  display: block;
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.shelf-card-ph {
  width: 100%;
  height: 100%;
  aspect-ratio: 1;
  background: color-mix(in srgb, var(--surface-1, #1a2221) 80%, var(--accent) 14%);
}
</style>

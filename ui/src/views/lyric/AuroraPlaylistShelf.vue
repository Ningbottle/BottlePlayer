<script setup lang="ts">
/**
 * CSS 3D playlist shelf — shown in fullscreen lyric stage.
 * No Three.js: perspective + rotateY carousel of queue cards.
 */
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue';
import { gsap } from 'gsap';
import type { Track } from '../../api/normalizer';
import { isReducedMotion } from '../../api/motion';

const props = defineProps<{
  open: boolean;
  tracks: Track[];
  activeHash: string | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'select', track: Track): void;
}>();

const rootRef = ref<HTMLElement | null>(null);
const stageRef = ref<HTMLElement | null>(null);
const focusIndex = ref(0);

const visibleTracks = computed(() => props.tracks.slice(0, 24));

const activeIndex = computed(() => {
  if (!props.activeHash) return 0;
  const i = visibleTracks.value.findIndex((t) => t.FileHash === props.activeHash);
  return i >= 0 ? i : 0;
});

watch(
  () => props.open,
  async (open) => {
    if (!open) return;
    focusIndex.value = activeIndex.value;
    await nextTick();
    playOpen();
  },
);

function angleFor(i: number): number {
  const n = Math.max(visibleTracks.value.length, 1);
  const step = 360 / Math.max(n, 8);
  return (i - focusIndex.value) * Math.min(step, 28);
}

function coverOf(t: Track): string {
  return t.Image || '';
}

function playOpen(): void {
  const root = rootRef.value;
  const stage = stageRef.value;
  if (!root) return;
  if (isReducedMotion()) {
    gsap.set(root, { opacity: 1 });
    return;
  }
  gsap.killTweensOf(root);
  if (stage) gsap.killTweensOf(stage);
  gsap.fromTo(
    root,
    { opacity: 0 },
    { opacity: 1, duration: 0.28, ease: 'power2.out' },
  );
  if (stage) {
    gsap.fromTo(
      stage,
      { rotateX: 18, y: 40, scale: 0.92 },
      { rotateX: 8, y: 0, scale: 1, duration: 0.55, ease: 'power3.out' },
    );
  }
}

function spinTo(i: number): void {
  focusIndex.value = Math.max(0, Math.min(visibleTracks.value.length - 1, i));
}

function onWheel(e: WheelEvent): void {
  if (!props.open) return;
  e.preventDefault();
  if (e.deltaY > 0) spinTo(focusIndex.value + 1);
  else spinTo(focusIndex.value - 1);
}

function onKey(e: KeyboardEvent): void {
  if (!props.open) return;
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
    if (t) emit('select', t);
  }
}

watch(
  () => props.open,
  (open) => {
    if (open) window.addEventListener('keydown', onKey);
    else window.removeEventListener('keydown', onKey);
  },
);

onBeforeUnmount(() => {
  window.removeEventListener('keydown', onKey);
});

function onSelect(t: Track, i: number): void {
  if (i !== focusIndex.value) {
    spinTo(i);
    return;
  }
  emit('select', t);
}
</script>

<template>
  <div
    v-if="open"
    ref="rootRef"
    class="shelf-root"
    data-test="aurora-playlist-shelf"
    role="dialog"
    aria-modal="true"
    aria-label="播放队列歌单架"
  >
    <button type="button" class="shelf-backdrop" aria-label="关闭歌单架" @click="emit('close')" />

    <div class="shelf-panel">
      <header class="shelf-head">
        <div>
          <p class="shelf-kicker">QUEUE · 3D</p>
          <h2 class="shelf-title">歌单架</h2>
        </div>
        <button type="button" class="shelf-close" data-test="shelf-close" @click="emit('close')">
          关闭
        </button>
      </header>

      <p v-if="!visibleTracks.length" class="shelf-empty">队列还是空的，先去播放几首歌吧</p>

      <div
        v-else
        ref="stageRef"
        class="shelf-stage"
        data-test="shelf-stage"
        @wheel.prevent="onWheel"
      >
        <div class="shelf-floor" aria-hidden="true" />
        <button
          v-for="(t, i) in visibleTracks"
          :key="t.FileHash || i"
          type="button"
          class="shelf-card"
          :class="{
            'is-focus': i === focusIndex,
            'is-active': t.FileHash === activeHash,
          }"
          :style="{
            transform: `translate(-50%, -50%) rotateY(${angleFor(i)}deg) translateZ(220px)`,
            zIndex: i === focusIndex ? 20 : 10 - Math.abs(i - focusIndex),
          }"
          :data-test="`shelf-card-${i}`"
          @click="onSelect(t, i)"
        >
          <div class="shelf-card-face">
            <img v-if="coverOf(t)" :src="coverOf(t)" :alt="t.SongName" />
            <div v-else class="shelf-card-ph">{{ (t.SongName || '?').slice(0, 1) }}</div>
            <div class="shelf-card-meta">
              <b>{{ t.SongName }}</b>
              <span>{{ t.SingerName }}</span>
            </div>
          </div>
        </button>
      </div>

      <p class="shelf-hint">滚轮 / ← → 切换 · Enter 播放 · Esc 关闭</p>
    </div>
  </div>
</template>

<style scoped>
.shelf-root {
  position: fixed;
  inset: 0;
  z-index: 40;
  display: grid;
  place-items: center;
}

.shelf-backdrop {
  position: absolute;
  inset: 0;
  border: 0;
  padding: 0;
  cursor: pointer;
  background: color-mix(in srgb, #020406 72%, transparent);
  backdrop-filter: blur(6px);
}

.shelf-panel {
  position: relative;
  z-index: 1;
  width: min(920px, 94vw);
  height: min(520px, 78vh);
  display: flex;
  flex-direction: column;
  border-radius: 20px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background:
    radial-gradient(ellipse 70% 50% at 50% 0%, color-mix(in srgb, var(--accent) 16%, transparent), transparent 60%),
    color-mix(in srgb, var(--surface-elevated, #0e1413) 94%, #000 6%);
  box-shadow: 0 30px 80px rgba(0, 0, 0, 0.55);
  padding: 18px 20px 14px;
  box-sizing: border-box;
}

.shelf-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  flex: none;
}

.shelf-kicker {
  margin: 0;
  font-size: 11px;
  letter-spacing: 0.16em;
  color: var(--accent);
  font-weight: 600;
}

.shelf-title {
  margin: 4px 0 0;
  font-size: 22px;
  font-weight: 700;
  color: var(--text-primary, #f2f5f2);
}

.shelf-close {
  border: 1px solid color-mix(in srgb, #fff 12%, transparent);
  background: color-mix(in srgb, #fff 6%, transparent);
  color: var(--text-secondary, #aab4af);
  border-radius: 999px;
  padding: 8px 14px;
  font: inherit;
  font-size: 13px;
  cursor: pointer;
}
.shelf-close:hover {
  color: var(--text-primary, #fff);
  border-color: var(--accent);
}

.shelf-empty {
  margin: auto;
  color: var(--text-muted, #6a7570);
  font-size: 14px;
}

.shelf-stage {
  position: relative;
  flex: 1;
  min-height: 0;
  margin-top: 8px;
  perspective: 900px;
  transform-style: preserve-3d;
  overflow: hidden;
}

.shelf-floor {
  position: absolute;
  left: 10%;
  right: 10%;
  bottom: 12%;
  height: 40%;
  background: radial-gradient(ellipse at 50% 0%, color-mix(in srgb, var(--accent) 18%, transparent), transparent 70%);
  transform: rotateX(72deg);
  pointer-events: none;
}

.shelf-card {
  position: absolute;
  left: 50%;
  top: 48%;
  width: min(160px, 28vw);
  border: 0;
  padding: 0;
  background: transparent;
  cursor: pointer;
  transform-style: preserve-3d;
  transition: transform 0.45s cubic-bezier(0.22, 1, 0.36, 1), opacity 0.35s ease, filter 0.35s ease;
  opacity: 0.55;
  filter: brightness(0.72);
}

.shelf-card.is-focus {
  opacity: 1;
  filter: brightness(1.05);
}

.shelf-card.is-active .shelf-card-face {
  outline: 2px solid color-mix(in srgb, var(--accent) 75%, transparent);
  outline-offset: 2px;
}

.shelf-card-face {
  border-radius: 12px;
  overflow: hidden;
  background: var(--surface-2, #141a1b);
  box-shadow:
    0 18px 36px rgba(0, 0, 0, 0.45),
    0 0 0 1px color-mix(in srgb, #fff 8%, transparent);
  transform: translateZ(0);
}

.shelf-card-face img {
  display: block;
  width: 100%;
  aspect-ratio: 1;
  object-fit: cover;
}

.shelf-card-ph {
  aspect-ratio: 1;
  display: grid;
  place-items: center;
  font-size: 42px;
  font-weight: 700;
  color: var(--accent);
  background: color-mix(in srgb, var(--surface-1, #1a2221) 80%, var(--accent) 10%);
}

.shelf-card-meta {
  padding: 8px 10px 10px;
  text-align: left;
}
.shelf-card-meta b {
  display: block;
  font-size: 12px;
  color: var(--text-primary, #f2f5f2);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.shelf-card-meta span {
  display: block;
  font-size: 11px;
  color: var(--text-muted, #7a8680);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  margin-top: 2px;
}

.shelf-hint {
  margin: 8px 0 0;
  text-align: center;
  font-size: 11px;
  letter-spacing: 0.04em;
  color: var(--text-muted, #6a7570);
  flex: none;
}
</style>

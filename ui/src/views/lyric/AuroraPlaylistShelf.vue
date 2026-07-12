<script setup lang="ts">
/**
 * CSS 3D playlist shelf — fullscreen lyric stage queue browser.
 * Teleported to body so fixed overlay is never clipped by shell overflow /
 * GSAP transform containing blocks on the lyric stage.
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
const stripRef = ref<HTMLElement | null>(null);
const focusIndex = ref(0);

const visibleTracks = computed(() => props.tracks.slice(0, 32));

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

function coverOf(t: Track): string {
  return t.Image || '';
}

/** Coverflow offset from focused card: translateX + rotateY + scale + opacity. */
function cardStyle(i: number): Record<string, string | number> {
  const offset = i - focusIndex.value;
  const abs = Math.abs(offset);
  const x = offset * 132;
  const rot = Math.max(-55, Math.min(55, offset * -32));
  const scale = Math.max(0.72, 1 - abs * 0.1);
  const z = 40 - abs;
  const opacity = abs > 4 ? 0 : Math.max(0.35, 1 - abs * 0.18);
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
  // Always ensure visible even if GSAP is interrupted
  root.style.opacity = '1';
  if (isReducedMotion()) {
    gsap.set(root, { opacity: 1 });
    return;
  }
  gsap.killTweensOf(root);
  if (strip) gsap.killTweensOf(strip);
  gsap.fromTo(
    root,
    { opacity: 0 },
    { opacity: 1, duration: 0.24, ease: 'power2.out' },
  );
  if (strip) {
    gsap.fromTo(
      strip,
      { y: 28, scale: 0.94 },
      { y: 0, scale: 1, duration: 0.45, ease: 'power3.out' },
    );
  }
}

function spinTo(i: number): void {
  focusIndex.value = Math.max(0, Math.min(visibleTracks.value.length - 1, i));
}

function onWheel(e: WheelEvent): void {
  if (!props.open) return;
  e.preventDefault();
  if (e.deltaY > 0 || e.deltaX > 0) spinTo(focusIndex.value + 1);
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
  <Teleport to="body">
    <div
      v-if="open"
      ref="rootRef"
      class="shelf-root"
      data-test="aurora-playlist-shelf"
      role="dialog"
      aria-modal="true"
      aria-label="播放队列歌单架"
    >
      <button
        type="button"
        class="shelf-backdrop"
        aria-label="关闭歌单架"
        data-test="shelf-backdrop"
        @click="emit('close')"
      />

      <div class="shelf-panel" data-test="shelf-panel">
        <header class="shelf-head">
          <div>
            <p class="shelf-kicker">QUEUE · 3D</p>
            <h2 class="shelf-title">歌单架</h2>
          </div>
          <div class="shelf-head-actions">
            <button
              type="button"
              class="shelf-nav"
              data-test="shelf-prev"
              :disabled="focusIndex <= 0"
              aria-label="上一张"
              @click="spinTo(focusIndex - 1)"
            >
              ‹
            </button>
            <button
              type="button"
              class="shelf-nav"
              data-test="shelf-next"
              :disabled="focusIndex >= visibleTracks.length - 1"
              aria-label="下一张"
              @click="spinTo(focusIndex + 1)"
            >
              ›
            </button>
            <button type="button" class="shelf-close" data-test="shelf-close" @click="emit('close')">
              关闭
            </button>
          </div>
        </header>

        <p v-if="!visibleTracks.length" class="shelf-empty" data-test="shelf-empty">
          队列还是空的，先去播放几首歌吧
        </p>

        <div
          v-else
          ref="stripRef"
          class="shelf-stage"
          data-test="shelf-stage"
          @wheel.prevent="onWheel"
        >
          <div class="shelf-floor" aria-hidden="true" />
          <div class="shelf-track">
            <button
              v-for="(t, i) in visibleTracks"
              :key="t.FileHash || i"
              type="button"
              class="shelf-card"
              :class="{
                'is-focus': i === focusIndex,
                'is-active': t.FileHash === activeHash,
              }"
              :style="cardStyle(i)"
              :data-test="`shelf-card-${i}`"
              :tabindex="i === focusIndex ? 0 : -1"
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
        </div>

        <div class="shelf-footer">
          <p class="shelf-count" data-test="shelf-count">
            {{ visibleTracks.length ? `${focusIndex + 1} / ${visibleTracks.length}` : '0' }}
          </p>
          <p class="shelf-hint">滚轮 / ← → 切换 · Enter 播放 · Esc 关闭</p>
          <button
            v-if="visibleTracks[focusIndex]"
            type="button"
            class="shelf-play"
            data-test="shelf-play"
            @click="emit('select', visibleTracks[focusIndex]!)"
          >
            播放此曲
          </button>
        </div>
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
}

.shelf-backdrop {
  position: absolute;
  inset: 0;
  border: 0;
  padding: 0;
  cursor: pointer;
  background: color-mix(in srgb, #020406 78%, transparent);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
}

.shelf-panel {
  position: relative;
  z-index: 1;
  width: min(960px, 94vw);
  height: min(560px, 82vh);
  display: flex;
  flex-direction: column;
  border-radius: 20px;
  border: 1px solid color-mix(in srgb, #fff 12%, transparent);
  background:
    radial-gradient(ellipse 70% 50% at 50% 0%, color-mix(in srgb, var(--accent) 18%, transparent), transparent 60%),
    color-mix(in srgb, var(--surface-elevated, #0e1413) 96%, #000 4%);
  box-shadow: 0 30px 90px rgba(0, 0, 0, 0.6);
  padding: 18px 20px 14px;
  box-sizing: border-box;
  pointer-events: auto;
}

.shelf-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  flex: none;
}

.shelf-head-actions {
  display: flex;
  align-items: center;
  gap: 8px;
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

.shelf-nav,
.shelf-close {
  border: 1px solid color-mix(in srgb, #fff 14%, transparent);
  background: color-mix(in srgb, #fff 7%, transparent);
  color: var(--text-secondary, #aab4af);
  border-radius: 8px;
  font: inherit;
  cursor: pointer;
  transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
}

.shelf-nav {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  font-size: 22px;
  line-height: 1;
  padding: 0;
}

.shelf-nav:disabled {
  opacity: 0.35;
  cursor: not-allowed;
}

.shelf-close {
  padding: 8px 14px;
  font-size: 13px;
  font-weight: 600;
}

.shelf-nav:hover:not(:disabled),
.shelf-close:hover {
  color: var(--text-primary, #fff);
  border-color: var(--accent);
  background: color-mix(in srgb, var(--accent) 14%, transparent);
}

.shelf-empty {
  margin: auto;
  color: var(--text-muted, #6a7570);
  font-size: 14px;
}

.shelf-stage {
  position: relative;
  flex: 1;
  min-height: 280px;
  margin-top: 10px;
  perspective: 1100px;
  overflow: visible;
}

.shelf-floor {
  position: absolute;
  left: 8%;
  right: 8%;
  bottom: 8%;
  height: 36%;
  background: radial-gradient(ellipse at 50% 0%, color-mix(in srgb, var(--accent) 22%, transparent), transparent 72%);
  pointer-events: none;
}

.shelf-track {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  transform-style: preserve-3d;
}

.shelf-card {
  position: absolute;
  left: 50%;
  top: 50%;
  width: min(180px, 34vw);
  margin-left: calc(min(180px, 34vw) / -2);
  margin-top: -118px;
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
  filter: brightness(1.06);
  cursor: pointer;
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
    0 20px 40px rgba(0, 0, 0, 0.5),
    0 0 0 1px color-mix(in srgb, #fff 10%, transparent);
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
  font-size: 48px;
  font-weight: 700;
  color: var(--accent);
  background: color-mix(in srgb, var(--surface-1, #1a2221) 80%, var(--accent) 12%);
}

.shelf-card-meta {
  padding: 10px 12px 12px;
  text-align: left;
}
.shelf-card-meta b {
  display: block;
  font-size: 13px;
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
  margin-top: 3px;
}

.shelf-footer {
  flex: none;
  display: grid;
  grid-template-columns: 1fr auto 1fr;
  align-items: center;
  gap: 10px;
  margin-top: 8px;
  min-height: 40px;
}

.shelf-count {
  margin: 0;
  font-size: 12px;
  font-variant-numeric: tabular-nums;
  color: var(--text-secondary, #aab4af);
}

.shelf-hint {
  margin: 0;
  text-align: center;
  font-size: 11px;
  letter-spacing: 0.03em;
  color: var(--text-muted, #6a7570);
}

.shelf-play {
  justify-self: end;
  padding: 8px 16px;
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--accent) 55%, transparent);
  background: color-mix(in srgb, var(--accent) 22%, transparent);
  color: var(--text-primary, #fff);
  font: inherit;
  font-size: 13px;
  font-weight: 650;
  cursor: pointer;
}
.shelf-play:hover {
  background: color-mix(in srgb, var(--accent) 36%, transparent);
  border-color: var(--accent);
}
</style>

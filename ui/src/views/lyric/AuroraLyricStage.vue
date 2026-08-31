<script setup lang="ts">
import { ref, watch, onMounted, onBeforeUnmount, nextTick, computed } from 'vue';
import { gsap } from 'gsap';
import { PhArrowsOutSimple, PhDisc, PhPause, PhPlay } from '@phosphor-icons/vue';
import { isReducedMotion, startVinylSpin } from '../../shared/motion/motion';
import type { VinylSpinHandle } from '../../shared/motion/motion';
import { useLyricFocusStore } from '../../api/lyricFocusStore';
import { playerStore, playTrack, togglePlay as storeTogglePlay } from '../../playback/index';
import type { Track } from '../../shared/music/track';
import type { LyricStageModel } from './useLyricStage';
import AuroraPlaylistShelf from './AuroraPlaylistShelf.vue';
import PlayerProgress from '../../playback/components/player/PlayerProgress.vue';
import FullscreenWindowControls from '../../app/shell/FullscreenWindowControls.vue';
import { useAutoHideControls } from './useAutoHideControls';

const props = defineProps<{ model: LyricStageModel }>();

const emit = defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'exit-fullscreen'): void;
  (e: 'user-scroll'): void;
  (e: 'seek-line', timeSeconds: number): void;
  (e: 'seek', timeSeconds: number): void;
}>();

const fullscreenActive = computed(() => props.model.fullscreen);
const autoHideControls = useAutoHideControls({
  active: fullscreenActive,
  onEscape: () => emit('exit-fullscreen'),
});
const controlsVisible = autoHideControls.visible;

const coverRef = ref<HTMLElement | null>(null);
const discEl = ref<HTMLElement | null>(null);
let vinylSpin: VinylSpinHandle | null = null;
const rootRef = ref<HTMLElement | null>(null);
const washRef = ref<HTMLElement | null>(null);
const focus = useLyricFocusStore();
const shelfOpen = ref(false);

const queueTracks = computed(() => playerStore.queue ?? []);

function openShelf(): void {
  shelfOpen.value = true;
}

function closeShelf(): void {
  shelfOpen.value = false;
}

function onCoverClick(): void {
  if (!props.model.fullscreen) return;
  openShelf();
}

function onSelectTrack(track: Track): void {
  playTrack(track);
  shelfOpen.value = false;
}

/** Click a timed lyric line → parent seeks via playerStore.seek. */
function onLineClick(line: { time: number; text: string }): void {
  if (!Number.isFinite(line.time) || line.time < 0) return;
  emit('seek-line', line.time);
}

function onLyricKeydown(e: KeyboardEvent): void {
  const lyrics = props.model.parsedLyrics;
  const current = props.model.activeIndex;
  if (e.key === 'ArrowDown') {
    e.preventDefault();
    const next = current + 1;
    if (next < lyrics.length) onLineClick(lyrics[next]);
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    const prev = current - 1;
    if (prev >= 0) onLineClick(lyrics[prev]);
  }
}

function onStageDblClick(e: MouseEvent): void {
  if (!props.model.fullscreen) return;
  const target = e.target as HTMLElement;
  if (
    target.closest('.lyric-line')
    || target.closest('.aurora-cover')
    || target.closest('.aurora-fs-controls')
    || target.closest('.aurora-fs-exit-row')
  ) {
    return;
  }
  emit('exit-fullscreen');
}

const reducedMotion = computed(() => isReducedMotion());
const hasCover = computed(() => !!props.model.coverUrl && !reducedMotion.value);
const showCoverWash = computed(
  () => props.model.fullscreen && !!props.model.coverUrl && !reducedMotion.value,
);

function clearFullscreenTransientStyles(): void {
  const root = rootRef.value;
  const cover = coverRef.value;
  const wash = washRef.value;

  if (root) {
    gsap.killTweensOf(root);
    gsap.set(root, { clearProps: 'filter,opacity,transform' });
  }
  if (cover) {
    gsap.killTweensOf(cover);
    gsap.set(cover, { clearProps: 'width,height,opacity,transform' });
  }
  if (wash) {
    gsap.killTweensOf(wash);
    gsap.set(wash, { clearProps: 'opacity' });
  }
}

watch(
  () => props.model.fullscreen,
  (fs) => {
    if (!fs) {
      shelfOpen.value = false;
      clearFullscreenTransientStyles();
    }
  },
);

/** Once per FileHash when lines have been staggered (async lyrics must not double-flash). */
const lineEnterDoneForHash = ref<string | null>(null);

function lineClass(idx: number): string {
  const active = props.model.activeIndex;
  if (idx === active) return 'active';
  const diff = Math.abs(idx - active);
  if (diff === 1) return 'near';
  if (diff === 2) return 'mid';
  return 'far';
}

/** Karaoke sweep across the active line, by line duration. */
const activeFillPct = computed(() => {
  const idx = props.model.activeIndex;
  const lines = props.model.parsedLyrics;
  const line = lines[idx];
  if (!line) return 0;
  const end = lines[idx + 1]?.time ?? line.time + 4;
  const span = Math.max(0.4, end - line.time);
  return Math.max(0, Math.min(100, ((props.model.currentTime - line.time) / span) * 100));
});

function queryLineEls(): Element[] {
  if (!rootRef.value) return [];
  return Array.from(rootRef.value.querySelectorAll('.lyric-line'));
}

/**
 * Unified page open: soft veil + content settle as one beat (no extra curtain layer).
 * Fast enough that follow snap (~0.5s) lands while enter still feels intentional.
 */
function playStageEnter(): void {
  const root = rootRef.value;
  const cover = coverRef.value;
  if (root) gsap.killTweensOf(root);
  if (cover) gsap.killTweensOf(cover);

  if (isReducedMotion()) {
    if (root) gsap.set(root, { opacity: 1, y: 0, clearProps: 'filter,opacity,transform' });
    if (cover) gsap.set(cover, { opacity: 1, scale: 1, y: 0, clearProps: 'opacity,transform' });
    return;
  }

  if (root) {
    gsap.fromTo(
      root,
      { opacity: 0, y: 28, filter: 'blur(8px)' },
      {
        opacity: 1,
        y: 0,
        filter: 'blur(0px)',
        duration: 0.58,
        ease: 'back.out(1.2)',
        onComplete: () => {
          if (rootRef.value) {
            rootRef.value.style.filter = 'none';
            rootRef.value.style.opacity = '';
            rootRef.value.style.transform = '';
          }
        },
      },
    );
  }
  if (cover) {
    gsap.fromTo(
      cover,
      { opacity: 0.3, y: 26, scale: 0.92 },
      {
        opacity: 1,
        y: 0,
        scale: 1,
        duration: 0.65,
        ease: 'back.out(1.6)',
        delay: 0.05,
        onComplete: () => {
          if (coverRef.value) {
            gsap.set(coverRef.value, { clearProps: 'opacity,transform' });
          }
        },
      },
    );
  }
}

/**
 * Lines ease in with the stage (short). Once per FileHash.
 */
function playLineEnter(fileHash: string): void {
  if (!fileHash || lineEnterDoneForHash.value === fileHash) return;
  if (props.model.parsedLyrics.length === 0) return;

  const lines = queryLineEls();
  if (lines.length === 0) return;

  lineEnterDoneForHash.value = fileHash;
  lines.forEach((el) => gsap.killTweensOf(el));

  if (isReducedMotion()) {
    gsap.set(lines, { opacity: 1, y: 0, clearProps: 'opacity,transform' });
    return;
  }

  gsap.fromTo(
    lines,
    { opacity: 0, y: 10 },
    {
      opacity: 1,
      y: 0,
      duration: 0.42,
      ease: 'back.out(1.3)',
      stagger: 0.03,
      delay: 0.06,
      clearProps: 'opacity,transform',
    },
  );
}

function tryPlayLineEnter(): void {
  const hash = props.model.currentTrack?.FileHash;
  if (!hash || props.model.parsedLyrics.length === 0) return;
  if (lineEnterDoneForHash.value === hash) return;
  void nextTick(() => {
    playLineEnter(hash);
  });
}

onMounted(() => {
  playStageEnter();
  tryPlayLineEnter();
  if (washRef.value) {
    gsap.set(washRef.value, { opacity: showCoverWash.value && !isReducedMotion() ? 0.9 : 0 });
  }
  if (discEl.value) {
    vinylSpin = startVinylSpin(discEl.value, () => !!props.model.isPlaying);
  }
});

watch(() => props.model.isPlaying, () => vinylSpin?.setPlaying());

watch(
  () => props.model.currentTrack?.FileHash,
  (hash) => {
    if (hash !== lineEnterDoneForHash.value) {
      lineEnterDoneForHash.value = null;
    }
    playStageEnter();
    tryPlayLineEnter();
  },
);

watch(
  () => props.model.parsedLyrics.length,
  (len) => {
    if (len <= 0) return;
    tryPlayLineEnter();
  },
);

watch(() => props.model.fullscreen, (fs) => {
  const cover = coverRef.value;
  if (!cover) return;
  if (!fs) return;
  if (isReducedMotion()) {
    gsap.set(cover, { clearProps: 'width,height' });
    return;
  }
  // Sizes come from CSS (non-fs large left cover / fs tall cover) — only clear inline overrides
  gsap.to(cover, {
    clearProps: 'width,height',
    duration: 0.35,
    ease: 'power2.out',
  });
  void fs;
}, { flush: 'post' });

watch(showCoverWash, (show) => {
  const wash = washRef.value;
  if (!wash) return;
  if (isReducedMotion()) {
    gsap.set(wash, { opacity: 0 });
    return;
  }
  if (show) {
    gsap.to(wash, { opacity: 0.9, duration: 0.6, ease: 'power2.out' });
  } else {
    gsap.to(wash, { opacity: 0, duration: 0.35, ease: 'power2.in' });
  }
}, { flush: 'post' });

watch(() => props.model.coverUrl, () => {
  const wash = washRef.value;
  if (!wash || !showCoverWash.value || isReducedMotion()) return;
  gsap.fromTo(wash, { opacity: 0 }, { opacity: 0.9, duration: 0.6, ease: 'power2.out' });
}, { flush: 'post' });

onBeforeUnmount(() => {
  vinylSpin?.kill();
  vinylSpin = null;
  clearFullscreenTransientStyles();
  autoHideControls.dispose();
});
</script>

<template>
  <!-- Always left cover + right lyrics (fullscreen only scales cover / padding) -->
  <div
    ref="rootRef"
    class="aurora-lyric-stage"
    :class="{ 'aurora-lyric-fullscreen': model.fullscreen }"
    :data-lyric-focus="focus.mode.value"
    data-test="aurora-lyric-stage"
    @pointermove="autoHideControls.onPointerMove"
    @focusin="autoHideControls.onFocusIn"
    @focusout="autoHideControls.onFocusOut"
    @dblclick="onStageDblClick"
  >
    <!-- Cover wash: always in DOM when cover exists; GSAP controls opacity fade -->
    <div
      v-if="hasCover"
      ref="washRef"
      class="lyric-cover-wash"
      data-test="lyric-cover-wash"
      aria-hidden="true"
      :style="{ backgroundImage: `url(${model.coverUrl})` }"
    />

    <div
      v-if="model.fullscreen"
      class="aurora-fs-readability"
      data-test="aurora-fs-readability"
      data-contrast="high"
      aria-hidden="true"
    />

    <div
      class="lyric-meta aurora-lyric-meta-column"
      data-test="lyric-meta-column"
      @dblclick="!model.fullscreen && emit('enter-fullscreen')"
    >
      <div
        class="aurora-cover"
        :class="{ 'is-shelf-hot': model.fullscreen }"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
        :aria-label="model.fullscreen ? '打开歌单架' : undefined"
        :role="model.fullscreen ? 'button' : undefined"
        :tabindex="model.fullscreen ? 0 : undefined"
        @click="onCoverClick"
        @keydown.enter.prevent="onCoverClick"
        @keydown.space.prevent="onCoverClick"
      >
        <div ref="discEl" class="lyric-vinyl-disc" aria-hidden="true">
          <img v-if="model.coverUrl" :src="model.coverUrl" alt="" />
          <PhDisc
            v-else
            class="aurora-cover-placeholder"
            data-test="lyric-cover-placeholder"
            data-icon-family="phosphor"
            :size="88"
            weight="duotone"
            aria-hidden="true"
          />
          <div class="lyric-vinyl-grooves" aria-hidden="true" />
        </div>
        <div class="lyric-vinyl-spindle" aria-hidden="true" />
      </div>
      <button
        v-if="!model.fullscreen"
        type="button"
        class="aurora-lyric-enter-fullscreen"
        data-test="lyric-enter-fullscreen"
        aria-label="进入全屏歌词"
        title="进入全屏歌词"
        @click.stop="emit('enter-fullscreen')"
      >
        <PhArrowsOutSimple :size="16" weight="bold" aria-hidden="true" />
      </button>
      <div class="aurora-lyric-track-meta">
        <span class="aurora-lyric-kicker">正在播放</span>
        <h2>{{ model.currentTrack?.SongName }}</h2>
        <p>{{ model.currentTrack?.SingerName }}</p>
        <small>{{ model.currentTrack?.AlbumName || '未知专辑' }}</small>
      </div>
      <!-- Fullscreen transport: under title/meta text in left column (auto-hide kept) -->
      <div
        v-if="model.fullscreen && model.duration > 0"
        class="aurora-fs-controls"
        :class="{ 'controls-visible': controlsVisible }"
        data-test="aurora-fs-controls"
        :data-visible="String(controlsVisible)"
        data-contrast="high"
        data-visual-weight="subtle"
        @click.stop
        @dblclick.stop
      >
        <button
          type="button"
          class="aurora-fs-play"
          :data-test="model.isPlaying ? 'aurora-fs-pause' : 'aurora-fs-play'"
          :aria-label="model.isPlaying ? '暂停' : '播放'"
          :title="model.isPlaying ? '暂停' : '播放'"
          @click="storeTogglePlay"
        >
          <PhPause v-if="model.isPlaying" :size="16" weight="fill" aria-hidden="true" />
          <PhPlay v-else :size="16" weight="fill" aria-hidden="true" />
        </button>
        <PlayerProgress
          :current-time="model.currentTime"
          :duration="model.duration"
          @seek="(s: number) => emit('seek', s)"
        />
      </div>
      <!-- Exit fullscreen only — under album/progress; window minimize stays top-right -->
      <div
        v-if="model.fullscreen"
        class="aurora-fs-exit-row"
        data-test="aurora-fs-exit-row"
        @click.stop
        @dblclick.stop
      >
        <FullscreenWindowControls :show-minimize="false" :show-exit="true" />
      </div>
    </div>

    <AuroraPlaylistShelf
      :open="shelfOpen"
      :tracks="queueTracks"
      :active-hash="model.currentTrack?.FileHash ?? null"
      @close="closeShelf"
      @select="onSelectTrack"
    />
    <div
      class="lyric-content-column aurora-lyric-content-column"
      data-test="lyric-content-column"
      data-layout="two-column"
    >
      <slot v-if="model.loading" name="loading" />
      <slot v-else-if="model.error" name="error" />
      <div
        v-else
        class="lyric-scroll"
        :class="{ paused: !model.autoFollowing }"
        data-test="lyric-scroll"
        tabindex="0"
        @wheel.passive="$emit('user-scroll')"
        @touchmove.passive="$emit('user-scroll')"
        @keydown="onLyricKeydown"
      >
        <button
          v-for="(line, idx) in model.parsedLyrics"
          :key="idx"
          type="button"
          :id="`lyric-line-${idx}`"
          :data-test="`lyric-line-${idx}`"
          class="lyric-line"
          :class="lineClass(idx)"
          @click="onLineClick(line)"
        >
          <span class="lyric-line-text">{{ line.text }}</span>
          <span
            v-if="idx === model.activeIndex"
            class="lyric-line-fill"
            :style="{ clipPath: `inset(0 ${100 - activeFillPct}% 0 0)` }"
            aria-hidden="true"
          >{{ line.text }}</span>
        </button>
      </div>
      <slot name="footer" />
    </div>

  </div>
</template>

<script lang="ts">
export default { name: 'AuroraLyricStage' };
</script>

<style scoped>
/*
  Cover (fixed) + lyrics (fill remaining). No locked lyric box, no right dead zone,
  no visible scrollbar (scroll still works).
*/
.aurora-lyric-stage {
  position: relative;
  isolation: isolate;
  display: flex;
  flex-direction: row;
  justify-content: flex-start;
  align-items: stretch;
  gap: clamp(28px, 3.5vw, 56px);
  height: 100%;
  min-height: min(640px, calc(100vh - 220px));
  /* modest inset — not glued to edges, not huge side voids */
  padding: 12px clamp(20px, 3vw, 40px) 12px clamp(24px, 4vw, 56px);
  box-sizing: border-box;
  width: 100%;
  overflow: hidden;
}

/* Soft album wash behind the stage — no chrome, pointer-events none */
.lyric-cover-wash {
  position: absolute;
  inset: -48px;
  z-index: 0;
  pointer-events: none;
  background-size: cover;
  background-position: center;
  filter: blur(56px) saturate(1.3);
  transform: scale(1.12);
  opacity: 0;
}

[data-mode='dark'] .lyric-cover-wash {
  filter: blur(56px) brightness(0.55) saturate(1.3);
}

[data-mode='light'] .lyric-cover-wash {
  filter: blur(56px) brightness(0.7) saturate(1.4);
}

.aurora-lyric-fullscreen {
  min-height: 100vh;
  height: 100vh;
  max-height: 100vh;
  overflow: hidden;
  width: 100%;
  gap: clamp(32px, 4vw, 64px);
  padding: clamp(20px, 3vh, 40px) clamp(24px, 3.5vw, 48px) clamp(20px, 3vh, 40px) clamp(28px, 4vw, 64px);
  background:
    radial-gradient(ellipse 80% 60% at 20% 40%, color-mix(in srgb, var(--accent) 22%, transparent), transparent 65%),
    radial-gradient(ellipse 60% 50% at 80% 70%, color-mix(in srgb, var(--accent) 14%, transparent), transparent 55%),
    var(--app-bg, #040607);
}

.aurora-lyric-fullscreen .lyric-meta,
.aurora-lyric-fullscreen .lyric-scroll {
  position: relative;
  z-index: 1;
}

.aurora-fs-readability {
  position: absolute;
  inset: 0;
  z-index: 0;
  pointer-events: none;
  background:
    linear-gradient(180deg, rgba(2, 5, 6, 0.2), rgba(2, 5, 6, 0.08) 42%, rgba(2, 5, 6, 0.48)),
    radial-gradient(ellipse 78% 58% at 50% 42%, transparent 0%, rgba(2, 5, 6, 0.18) 100%);
}

[data-mode='light'] .aurora-fs-readability {
  background:
    linear-gradient(180deg, rgba(255, 255, 255, 0.14), rgba(255, 255, 255, 0.04) 42%, rgba(255, 255, 255, 0.22)),
    radial-gradient(ellipse 78% 58% at 50% 42%, transparent 0%, rgba(255, 255, 255, 0.12) 100%);
}

[data-mode='dark'] .aurora-lyric-fullscreen {
  background:
    radial-gradient(ellipse 80% 60% at 20% 40%, color-mix(in srgb, var(--accent) 30%, transparent), transparent 65%),
    radial-gradient(ellipse 60% 50% at 80% 70%, color-mix(in srgb, var(--accent) 20%, transparent), transparent 55%),
    var(--app-bg, #040607);
}

.aurora-lyric-fullscreen .lyric-line {
  color: var(--text-secondary, #929c98);
  text-shadow: 0 1px 14px rgba(0, 0, 0, 0.32);
}

.aurora-lyric-fullscreen .lyric-line.near { opacity: 0.86; }
.aurora-lyric-fullscreen .lyric-line.mid { opacity: 0.74; }
.aurora-lyric-fullscreen .lyric-line.far { opacity: 0.66; }

[data-mode='dark'] .aurora-lyric-fullscreen .lyric-line {
  color: color-mix(in srgb, var(--text-primary) 78%, var(--accent) 22%);
}

.lyric-meta {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  flex: 0 0 auto;
  min-width: 0;
  height: 100%;
  padding: 0;
  box-sizing: border-box;
}

.aurora-lyric-meta-column {
  flex: 0 1 38%;
  gap: 10px;
}

.aurora-lyric-track-meta {
  width: min(100%, 380px);
  text-align: center;
}

.aurora-lyric-kicker {
  display: block;
  margin-bottom: 5px;
  color: var(--accent);
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.1em;
}

.aurora-lyric-track-meta h2,
.aurora-lyric-track-meta p,
.aurora-lyric-track-meta small {
  display: block;
  margin: 0;
}

.aurora-lyric-track-meta h2 {
  color: var(--text-primary);
  font-size: clamp(20px, 2.2vw, 30px);
  line-height: 1.2;
}

.aurora-lyric-track-meta p {
  margin-top: 5px;
  color: var(--text-secondary);
  font-size: 14px;
}

.aurora-lyric-track-meta small {
  margin-top: 4px;
  color: var(--text-muted);
  font-size: 11px;
}

.aurora-lyric-enter-fullscreen {
  width: 30px;
  height: 24px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 1px solid color-mix(in srgb, var(--text-primary) 12%, transparent);
  border-radius: 8px;
  background: color-mix(in srgb, var(--surface-elevated) 24%, transparent);
  color: var(--text-muted);
  cursor: pointer;
  line-height: 0;
  transition: color 0.15s ease, border-color 0.15s ease, opacity 0.15s ease;
}

.aurora-lyric-enter-fullscreen:hover,
.aurora-lyric-enter-fullscreen:focus-visible {
  color: var(--text-primary);
  border-color: color-mix(in srgb, var(--accent) 38%, transparent);
}

.aurora-lyric-enter-fullscreen:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

.aurora-cover {
  position: relative;
  width: min(34vw, 46vh, 380px);
  height: auto;
  aspect-ratio: 1;
  border-radius: 50%;
  box-shadow:
    0 28px 70px rgba(0, 0, 0, 0.42),
    0 0 0 1px color-mix(in srgb, #fff 5%, transparent);
  margin: 0;
  background: #0a0a09;
  flex: none;
}

/* Rotating disc: cover art + grooves (spindle stays static above) */
.lyric-vinyl-disc {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  overflow: hidden;
  will-change: transform;
}

.lyric-vinyl-disc img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
  border-radius: 50%;
}

.lyric-vinyl-grooves {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background:
    conic-gradient(from 210deg,
      transparent 0deg,
      color-mix(in srgb, var(--accent) 14%, transparent) 18deg,
      transparent 55deg),
    repeating-radial-gradient(circle at 50% 50%,
      rgba(255, 255, 255, 0.05) 0 1px,
      transparent 1px 4px);
  pointer-events: none;
}

.lyric-vinyl-spindle {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 26%;
  aspect-ratio: 1;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%,
    var(--app-bg) 0 11%,
    color-mix(in srgb, var(--accent) 82%, #000 18%) 12% 100%);
  box-shadow: 0 0 0 1px color-mix(in srgb, #fff 8%, transparent);
  pointer-events: none;
  z-index: 1;
}

.aurora-cover.is-shelf-hot {
  cursor: pointer;
}
.aurora-cover.is-shelf-hot:hover {
  box-shadow:
    0 26px 64px rgba(0, 0, 0, 0.45),
    0 0 0 1px color-mix(in srgb, var(--accent) 45%, transparent),
    0 0 32px color-mix(in srgb, var(--accent) 24%, transparent);
}

.aurora-lyric-fullscreen .aurora-cover {
  width: min(36vw, 50vh, 420px);
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.lyric-scroll {
  /* Take ALL remaining width after cover — no max-width cage, no empty right slab */
  flex: 1 1 0;
  width: auto;
  max-width: none;
  min-width: 0;
  min-height: 0;
  height: 100%;
  overflow-y: auto;
  overflow-x: hidden;
  /* Keep end-of-song follow from chaining scroll into the page shell */
  overscroll-behavior: contain;
  display: flex;
  flex-direction: column;
  gap: 18px;
  align-items: center;
  justify-content: flex-start;
  padding: min(14vh, 96px) 12px min(14vh, 96px);
  box-sizing: border-box;
  scrollbar-width: none;
  scrollbar-gutter: auto;
  -ms-overflow-style: none;
}

.aurora-cover-placeholder {
  position: absolute;
  top: 50%;
  left: 50%;
  width: 28%;
  height: 28%;
  color: var(--accent);
  opacity: 0.5;
  transform: translate(-50%, -50%);
}

.aurora-cover.is-shelf-hot:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 3px;
}

.lyric-content-column {
  flex: 1 1 0;
  min-width: 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.lyric-content-column .lyric-scroll {
  flex: 1 1 0;
}

.lyric-scroll::-webkit-scrollbar {
  width: 0;
  height: 0;
  display: none;
}

/* Stage mode: stronger edge fade; readable softens / removes mask for scanability */
.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-scroll {
  mask-image: linear-gradient(to bottom, transparent, white 10%, white 90%, transparent);
  -webkit-mask-image: linear-gradient(to bottom, transparent, white 10%, white 90%, transparent);
}

.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-scroll {
  mask-image: none;
  -webkit-mask-image: none;
}

.aurora-lyric-fullscreen .lyric-scroll {
  padding: min(22vh, 160px) 16px min(22vh, 160px);
}

.lyric-line {
  position: relative;
  width: 100%;
  /* Apple-style hanging indent: wrapped continuation lines indent ~2 chars */
  padding-left: 1.5em;
  text-indent: -1.5em;
  max-width: min(44ch, 94%);
  font-size: 20px;
  color: var(--text-muted, var(--ink-mute));
  text-align: center;
  font-family: var(--font-serif, serif);
  line-height: 1.7;
  transition: color 0.35s ease, opacity 0.35s ease, transform 0.35s ease, font-size 0.35s ease;
  /* Reset button chrome — clickable for seek */
  border: 0;
  background: transparent;
  padding: 0;
  margin: 0;
  cursor: pointer;
  font: inherit;
  appearance: none;
  -webkit-appearance: none;
}
.lyric-line:hover {
  color: color-mix(in srgb, var(--text-primary, #fff) 72%, var(--text-muted, #888) 28%);
}

.lyric-line.active {
  color: color-mix(in srgb, var(--text-primary, #fff) 38%, transparent);
  font-size: 28px;
  font-weight: 700;
  transform: scale(1.05);
  opacity: 1;
}

/* Karaoke sweep: bright text revealed left → right across the active line.
   Neutral (not accent) so custom accents never fight the lyric. */
.lyric-line-fill {
  position: absolute;
  inset: 0;
  padding-left: 1.5em;
  color: var(--text-primary, #fff);
  pointer-events: none;
  transition: clip-path 0.3s linear;
}

.aurora-lyric-fullscreen .lyric-line.active {
  font-size: clamp(32px, 3.8vw, 44px);
}

.aurora-lyric-fullscreen .lyric-line {
  font-size: clamp(20px, 2vw, 26px);
  max-width: min(50ch, 96%);
}

/* Readable: all lines scannable — far floor ≥ 0.72 */
.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.near {
  opacity: 0.92;
  font-size: 19px;
}

.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.mid {
  opacity: 0.84;
  font-size: 18px;
}

.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.far {
  opacity: 0.78;
  font-size: 17px;
}

/* Stage: near/far hierarchy with far floor ≥ 0.45 */
.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.near {
  opacity: 0.7;
  font-size: 19px;
}

.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.mid {
  opacity: 0.55;
  font-size: 18px;
}

.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.far {
  opacity: 0.48;
  font-size: 17px;
}

@media (max-width: 900px) {
  .aurora-lyric-stage,
  .aurora-lyric-fullscreen {
    flex-direction: row;
    justify-content: flex-start;
    gap: 14px;
    padding: 12px;
  }

  .aurora-cover,
  .aurora-lyric-fullscreen .aurora-cover {
    width: min(34vw, 240px, 32vh);
  }

  .aurora-lyric-meta-column {
    flex: 0 1 38%;
  }

  .aurora-lyric-track-meta h2 {
    font-size: clamp(17px, 3vw, 22px);
  }

  .lyric-scroll,
  .aurora-lyric-fullscreen .lyric-scroll {
    width: auto;
    height: 100%;
    padding-inline: 4px;
  }
}

/* Fullscreen mini transport: left column, under title/meta text (auto-hide). */
.aurora-fs-controls {
  flex: 0 0 auto;
  /* Match cover/meta column width so the bar aligns under the text block */
  width: min(34vw, 46vh, 380px);
  max-width: 100%;
  align-self: center;
  margin-top: 12px;
  margin-bottom: 2px;
  padding: 2px 5px;
  box-sizing: border-box;
  z-index: 2;
  display: flex;
  align-items: center;
  gap: 9px;
  border: 1px solid color-mix(in srgb, var(--text-primary) 10%, transparent);
  border-radius: 8px;
  background: color-mix(in srgb, var(--surface-elevated, var(--app-bg)) 20%, transparent);
  backdrop-filter: blur(6px);
  opacity: 0;
  pointer-events: none;
  transform: translateY(6px);
  transition: opacity 0.22s ease, transform 0.22s ease, border-color 0.2s ease;
}

.aurora-lyric-fullscreen .aurora-fs-controls {
  width: min(36vw, 50vh, 420px);
}

/* Exit-fullscreen only: under album text + progress; minimize stays top-right */
.aurora-fs-exit-row {
  flex: 0 0 auto;
  width: min(34vw, 46vh, 380px);
  max-width: 100%;
  align-self: center;
  margin-top: 8px;
  display: flex;
  justify-content: center;
  z-index: 2;
}

.aurora-lyric-fullscreen .aurora-fs-exit-row {
  width: min(36vw, 50vh, 420px);
}

.aurora-fs-exit-row :deep(.fs-controls) {
  display: flex;
  gap: 8px;
  justify-content: center;
}

.aurora-fs-controls.controls-visible,
.aurora-fs-controls:hover,
.aurora-fs-controls:focus-within,
.aurora-fs-play:focus-visible {
  opacity: 0.82;
  pointer-events: auto;
  transform: translateY(0);
  border-color: color-mix(in srgb, var(--text-primary) 26%, transparent);
}

.aurora-fs-controls:hover,
.aurora-fs-controls:focus-within {
  opacity: 1;
}

.aurora-fs-controls :deep(.progress-time) {
  color: var(--text-secondary);
  text-shadow: 0 1px 8px rgba(0, 0, 0, 0.28);
}

.aurora-fs-controls :deep(.progress-track)::before {
  height: 3px;
  background: color-mix(in srgb, var(--text-primary) 30%, var(--progress-track));
}

.aurora-fs-play {
  flex: 0 0 auto;
  width: 28px;
  height: 28px;
  border: 1px solid transparent;
  border-radius: 50%;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.2s ease, transform 0.15s ease;
}

.aurora-fs-play:hover {
  background: color-mix(in srgb, var(--text-primary) 15%, transparent);
  border-color: color-mix(in srgb, var(--text-primary) 46%, transparent);
  transform: scale(1.04);
}

.aurora-fs-play:focus-visible {
  outline: 2px solid var(--text-primary);
  outline-offset: 2px;
}

.aurora-fs-play:active {
  transform: scale(0.96);
}

.aurora-fs-play svg {
  width: 16px;
  height: 16px;
}

@media (prefers-reduced-motion: reduce) {
  .aurora-fs-controls {
    transform: none;
    transition: none;
  }
}
</style>

<script setup lang="ts">
import { ref, watch, onMounted, nextTick, computed } from 'vue';
import { gsap } from 'gsap';
import { isReducedMotion } from '../../api/motion';
import { useLyricFocusStore } from '../../api/lyricFocusStore';
import { playerStore, playTrack } from '../../api/playerStore';
import type { Track } from '../../api/normalizer';
import type { LyricStageModel } from './useLyricStage';
import CoverWebGLParticles from './CoverWebGLParticles.vue';
import AuroraPlaylistShelf from './AuroraPlaylistShelf.vue';
import PlayerProgress from '../../components/player/PlayerProgress.vue';

const props = defineProps<{ model: LyricStageModel }>();

const emit = defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'exit-fullscreen'): void;
  (e: 'user-scroll'): void;
  (e: 'seek-line', timeSeconds: number): void;
  (e: 'seek', timeSeconds: number): void;
}>();

const coverRef = ref<HTMLElement | null>(null);
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

function onStageDblClick(e: MouseEvent): void {
  if (!props.model.fullscreen) return;
  const target = e.target as HTMLElement;
  if (target.closest('.lyric-line') || target.closest('.aurora-cover')) return;
  emit('exit-fullscreen');
}

const reducedMotion = computed(() => isReducedMotion());
const hasCover = computed(() => !!props.model.coverUrl && !reducedMotion.value);
const showCoverWash = computed(
  () => props.model.fullscreen && !!props.model.coverUrl && !reducedMotion.value,
);

watch(
  () => props.model.fullscreen,
  (fs) => {
    if (!fs) shelfOpen.value = false;
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
});

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
</script>

<template>
  <!-- Always left cover + right lyrics (fullscreen only scales cover / padding) -->
  <div
    ref="rootRef"
    class="aurora-lyric-stage"
    :class="{ 'aurora-lyric-fullscreen': model.fullscreen }"
    :data-lyric-focus="focus.mode.value"
    data-test="aurora-lyric-stage"
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

    <!-- Cover only — no title/artist/hints/buttons. Dblclick → fullscreen; fs click → shelf. -->
    <div
      class="lyric-meta"
      data-test="lyric-meta"
      @dblclick="!model.fullscreen && emit('enter-fullscreen')"
    >
      <div
        class="aurora-cover is-shelf-hot"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
        aria-label="打开歌单架"
        @click="onCoverClick"
      >
        <img :src="model.coverUrl" alt="" />
        <CoverWebGLParticles
          :active="model.fullscreen"
          :is-playing="model.isPlaying"
        />
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
      class="lyric-scroll"
      :class="{ paused: !model.autoFollowing }"
      data-test="lyric-scroll"
      @wheel.passive="$emit('user-scroll')"
      @touchmove.passive="$emit('user-scroll')"
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
        {{ line.text }}
      </button>
    </div>

    <div
      v-if="model.fullscreen && model.duration > 0"
      class="aurora-fs-progress"
      data-test="aurora-fs-progress"
    >
      <PlayerProgress
        :current-time="model.currentTime"
        :duration="model.duration"
        @seek="(s: number) => emit('seek', s)"
      />
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
  filter: blur(56px) brightness(0.4) saturate(1.3);
}

[data-mode='light'] .lyric-cover-wash {
  filter: blur(56px) brightness(0.7) saturate(1.4);
}

.aurora-lyric-fullscreen {
  min-height: 100vh;
  height: 100vh;
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

.aurora-cover {
  position: relative;
  width: min(34vw, 46vh, 380px);
  height: auto;
  aspect-ratio: 1;
  border-radius: 18px;
  overflow: hidden;
  box-shadow:
    0 28px 70px rgba(0, 0, 0, 0.42),
    0 0 0 1px color-mix(in srgb, var(--accent) 22%, transparent);
  margin: 0;
  background: var(--surface-1, var(--paper-2));
  flex: none;
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
  width: 100%;
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
  color: var(--accent);
  font-size: 28px;
  font-weight: 700;
  transform: scale(1.05);
  text-shadow: 0 0 28px color-mix(in srgb, var(--accent) 35%, transparent);
  opacity: 1;
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
    flex-direction: column;
    justify-content: flex-start;
    gap: 20px;
    padding: 16px clamp(16px, 4vw, 32px);
  }

  .aurora-cover,
  .aurora-lyric-fullscreen .aurora-cover {
    width: min(56vw, 300px, 36vh);
  }

  .lyric-scroll,
  .aurora-lyric-fullscreen .lyric-scroll {
    flex: 1 1 auto;
    width: 100%;
    height: auto;
  }
}

.aurora-fs-progress {
  position: absolute;
  bottom: clamp(12px, 2.5vh, 24px);
  left: 50%;
  transform: translateX(-50%);
  width: min(520px, 60%);
  z-index: 2;
  opacity: 0.5;
  transition: opacity 0.3s ease;
}
.aurora-fs-progress:hover {
  opacity: 1;
}
</style>

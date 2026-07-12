<script setup lang="ts">
import { ref, watch, onMounted, nextTick, computed } from 'vue';
import { gsap } from 'gsap';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';
import { useLyricFocusStore } from '../../api/lyricFocusStore';
import { playerStore, playTrack } from '../../api/playerStore';
import type { Track } from '../../api/normalizer';
import type { LyricStageModel } from './useLyricStage';
import CoverWebGLParticles from './CoverWebGLParticles.vue';
import AuroraPlaylistShelf from './AuroraPlaylistShelf.vue';

const props = defineProps<{ model: LyricStageModel }>();

const emit = defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'user-scroll'): void;
}>();

const coverRef = ref<HTMLElement | null>(null);
const rootRef = ref<HTMLElement | null>(null);
const focus = useLyricFocusStore();
const shelfOpen = ref(false);

const queueTracks = computed(() => playerStore.queue ?? []);

function openShelf(): void {
  if (!props.model.fullscreen) return;
  shelfOpen.value = true;
}

function closeShelf(): void {
  shelfOpen.value = false;
}

function onCoverClick(): void {
  // Fullscreen: click cover opens 3D shelf; non-fs uses dblclick to enter fullscreen
  if (props.model.fullscreen) openShelf();
}

function onSelectTrack(track: Track): void {
  playTrack(track);
  shelfOpen.value = false;
}

watch(
  () => props.model.fullscreen,
  (fs) => {
    if (!fs) shelfOpen.value = false;
  },
);

/** Once per FileHash when lines have been staggered (async lyrics must not double-flash). */
const lineEnterDoneForHash = ref<string | null>(null);

const profile = getMotionProfile('aurora');

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
      { opacity: 0, y: 22, filter: 'blur(6px)' },
      {
        opacity: 1,
        y: 0,
        filter: 'blur(0px)',
        duration: 0.52,
        ease: 'power3.out',
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
      { opacity: 0.4, y: 20, scale: 0.96 },
      {
        opacity: 1,
        y: 0,
        scale: 1,
        duration: 0.55,
        ease: 'power3.out',
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
      duration: 0.36,
      ease: 'power2.out',
      stagger: 0.028,
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
</script>

<template>
  <!-- Always left cover + right lyrics (fullscreen only scales cover / padding) -->
  <div
    ref="rootRef"
    class="aurora-lyric-stage"
    :class="{ 'aurora-lyric-fullscreen': model.fullscreen }"
    :data-lyric-focus="focus.mode.value"
    data-test="aurora-lyric-stage"
  >
    <!-- Cover only — no title/artist/hints/buttons. Dblclick → fullscreen; fs click → shelf. -->
    <div
      class="lyric-meta"
      data-test="lyric-meta"
      @dblclick="!model.fullscreen && emit('enter-fullscreen')"
    >
      <div
        class="big-cover aurora-cover"
        ref="coverRef"
        data-test="lyric-cover"
        :class="{ 'is-shelf-hot': model.fullscreen }"
        :style="{ aspectRatio: '1' }"
        :aria-label="model.fullscreen ? '打开歌单架' : '封面'"
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
      :open="shelfOpen && model.fullscreen"
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
      <div
        v-for="(line, idx) in model.parsedLyrics"
        :key="idx"
        :id="`lyric-line-${idx}`"
        :data-test="`lyric-line-${idx}`"
        class="lyric-line"
        :class="lineClass(idx)"
      >
        {{ line.text }}
      </div>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'AuroraLyricStage' };
</script>

<style scoped>
.aurora-lyric-stage {
  display: grid;
  /* Non-fs: left rail mostly for a large cover */
  grid-template-columns: minmax(360px, 50%) minmax(0, 1fr);
  grid-template-rows: 1fr;
  gap: clamp(12px, 2vw, 28px);
  height: 100%;
  min-height: min(640px, calc(100vh - 220px));
  padding: 12px 8px 8px;
  align-items: stretch;
  box-sizing: border-box;
}

/* Fullscreen: fill the entire window — no floating island of content */
.aurora-lyric-fullscreen {
  grid-template-columns: minmax(340px, 0.44fr) minmax(0, 1fr);
  gap: clamp(16px, 2vw, 32px);
  padding: clamp(20px, 2.5vh, 36px) clamp(24px, 3vw, 48px);
  min-height: 100vh;
  height: 100vh;
  width: 100%;
  background:
    radial-gradient(ellipse 80% 60% at 20% 40%, color-mix(in srgb, var(--accent) 12%, transparent), transparent 60%),
    var(--app-bg, #040607);
}

.lyric-meta {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-width: 0;
  height: 100%;
  padding: 4px;
  box-sizing: border-box;
}

/* Fullscreen cover sits a touch toward the lyrics (right of the left rail) */
.aurora-lyric-fullscreen .lyric-meta {
  align-items: flex-end;
  padding-right: clamp(8px, 1.5vw, 20px);
  padding-left: clamp(12px, 2vw, 28px);
}

.aurora-cover {
  position: relative;
  /* Non-fs: dominate the left column */
  width: min(100%, 600px, 72vh);
  height: auto;
  aspect-ratio: 1;
  border-radius: 20px;
  overflow: hidden;
  box-shadow:
    0 24px 60px rgba(0, 0, 0, 0.4),
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
    0 22px 56px rgba(0, 0, 0, 0.42),
    0 0 0 1px color-mix(in srgb, var(--accent) 45%, transparent),
    0 0 28px color-mix(in srgb, var(--accent) 22%, transparent);
}

.aurora-lyric-fullscreen .aurora-cover {
  width: min(62vh, 580px);
  max-width: 100%;
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.lyric-scroll {
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  gap: 18px;
  align-items: center;
  /* Always flex-start — center + overflow clips the first half of long lyrics */
  justify-content: flex-start;
  min-height: 0;
  height: 100%;
  padding: min(18vh, 120px) 12px 80px;
  padding-bottom: min(18vh, 120px);
  scrollbar-width: thin;
  scrollbar-gutter: stable;
  scrollbar-color: color-mix(in srgb, var(--text-muted, #888) 45%, transparent) transparent;
}

.lyric-scroll::-webkit-scrollbar {
  width: 6px;
}
.lyric-scroll::-webkit-scrollbar-track {
  background: transparent;
}
.lyric-scroll::-webkit-scrollbar-thumb {
  background: color-mix(in srgb, var(--text-muted, #888) 40%, transparent);
  border-radius: 999px;
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

/* Fullscreen: immersive — hide scrollbar (scroll still works); never center-flex clip */
.aurora-lyric-fullscreen .lyric-scroll {
  padding: min(28vh, 200px) clamp(16px, 3vw, 40px) min(28vh, 200px);
  justify-content: flex-start;
  scrollbar-width: none; /* Firefox */
  scrollbar-gutter: auto;
}
.aurora-lyric-fullscreen .lyric-scroll::-webkit-scrollbar {
  width: 0;
  height: 0;
  display: none;
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
  .aurora-lyric-stage {
    grid-template-columns: 1fr;
    grid-template-rows: auto 1fr;
    gap: 16px;
  }

  .aurora-cover {
    width: min(320px, 78vw);
  }

  .aurora-lyric-fullscreen .lyric-meta {
    align-items: center;
    padding-right: 0;
    padding-left: 0;
  }
}
</style>

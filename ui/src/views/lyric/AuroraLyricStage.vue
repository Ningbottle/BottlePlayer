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
        :title="model.fullscreen ? '点击打开 3D 歌单架' : undefined"
        @click="onCoverClick"
      >
        <img :src="model.coverUrl" alt="cover" />
        <CoverWebGLParticles
          :active="model.fullscreen"
          :is-playing="model.isPlaying"
        />
      </div>
      <div class="lyric-meta-text">
        <h2 class="aurora-song-title">{{ model.currentTrack?.SongName }}</h2>
        <p class="aurora-artist">{{ model.currentTrack?.SingerName }}</p>
      </div>
      <!-- Bottom chrome under cover: square action chips, one neat row -->
      <div class="lyric-meta-actions" data-test="lyric-meta-actions">
        <button
          v-if="!model.fullscreen"
          type="button"
          class="lyric-action-btn"
          data-test="lyric-focus-toggle"
          :aria-pressed="focus.mode.value === 'stage'"
          :aria-label="focus.mode.value === 'readable' ? '切换为舞台渐隐' : '切换为清晰可读'"
          @click="focus.toggle()"
        >
          {{ focus.mode.value === 'readable' ? '清晰' : '舞台' }}
        </button>
        <button
          v-if="!model.fullscreen"
          type="button"
          class="lyric-action-btn lyric-action-primary"
          data-test="lyric-enter-fs"
          @click="emit('enter-fullscreen')"
        >
          全屏歌词
        </button>
        <button
          v-if="model.fullscreen"
          type="button"
          class="lyric-action-btn lyric-action-primary"
          data-test="lyric-shelf-open"
          @click.stop="openShelf"
        >
          歌单架
        </button>
        <p v-if="!model.fullscreen" class="aurora-fs-hint">也可双击封面进入全屏</p>
        <p v-else class="aurora-fs-hint">点击封面也可打开歌单架</p>
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
  /* Non-fs: wider left rail so the large cover owns most of the left space */
  grid-template-columns: minmax(300px, 42%) minmax(0, 1fr);
  grid-template-rows: 1fr;
  gap: clamp(16px, 2.5vw, 36px);
  height: 100%;
  min-height: min(640px, calc(100vh - 220px));
  padding: 12px 8px 8px;
  align-items: stretch;
  box-sizing: border-box;
}

/* Fullscreen: fill the entire window — no floating island of content */
.aurora-lyric-fullscreen {
  grid-template-columns: minmax(280px, 0.36fr) minmax(0, 1fr);
  gap: clamp(20px, 2.5vw, 40px);
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
  gap: 0;
  padding: 8px 4px;
  box-sizing: border-box;
}

.aurora-cover {
  position: relative;
  /* Non-fs: fill most of the left column — large square art */
  width: min(100%, 420px, 52vh);
  height: auto;
  aspect-ratio: 1;
  border-radius: 18px;
  overflow: hidden;
  box-shadow:
    0 22px 56px rgba(0, 0, 0, 0.38),
    0 0 0 1px color-mix(in srgb, var(--accent) 22%, transparent);
  margin-bottom: 16px;
  background: var(--surface-1, var(--paper-2));
  flex: none;
}

.aurora-cover.is-shelf-hot {
  cursor: pointer;
}
.aurora-cover.is-shelf-hot:hover {
  box-shadow:
    0 20px 52px rgba(0, 0, 0, 0.4),
    0 0 0 1px color-mix(in srgb, var(--accent) 45%, transparent),
    0 0 28px color-mix(in srgb, var(--accent) 22%, transparent);
}

.aurora-lyric-fullscreen .aurora-cover {
  width: min(46vh, 440px);
  max-width: 100%;
  margin-bottom: 14px;
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.lyric-meta-text {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  width: 100%;
  max-width: min(100%, 420px);
  margin-bottom: 12px;
}

.aurora-song-title {
  font-size: clamp(18px, 1.7vw, 26px);
  font-weight: 700;
  margin: 0;
  text-align: center;
  color: var(--text-primary);
  max-width: 20ch;
  line-height: 1.25;
}

.aurora-artist {
  font-size: 14px;
  color: var(--text-secondary, var(--ink-soft));
  margin: 0;
  text-align: center;
}

/* Square action chips under the art — aligned row */
.lyric-meta-actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  justify-content: center;
  gap: 8px;
  width: 100%;
  max-width: min(100%, 420px);
  margin-top: 2px;
}

.lyric-action-btn {
  min-width: 72px;
  height: 34px;
  padding: 0 14px;
  border-radius: 8px;
  border: 1px solid color-mix(in srgb, var(--text-primary) 14%, transparent);
  background: color-mix(in srgb, var(--surface-1, var(--paper-2)) 90%, transparent);
  color: var(--text-secondary, var(--ink-soft));
  font: inherit;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.03em;
  cursor: pointer;
  box-sizing: border-box;
  transition: color 0.15s ease, border-color 0.15s ease, background 0.15s ease;
}

.lyric-action-btn:hover {
  color: var(--text-primary, var(--ink));
  border-color: color-mix(in srgb, var(--accent) 50%, transparent);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
}

.lyric-action-btn[aria-pressed='true'] {
  color: var(--accent);
  border-color: color-mix(in srgb, var(--accent) 55%, transparent);
  background: color-mix(in srgb, var(--accent) 12%, transparent);
}

.lyric-action-primary {
  color: var(--text-primary);
  border-color: color-mix(in srgb, var(--accent) 48%, transparent);
  background: color-mix(in srgb, var(--accent) 16%, transparent);
}

.lyric-action-primary:hover {
  background: color-mix(in srgb, var(--accent) 28%, transparent);
  border-color: var(--accent);
}

.aurora-fs-hint {
  margin: 0;
  flex: 1 0 100%;
  text-align: center;
  font-size: 11px;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  opacity: 0.72;
  padding-top: 4px;
}

.lyric-scroll {
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  gap: 16px;
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
  max-width: min(42ch, 92%);
  font-size: 17px;
  color: var(--text-muted, var(--ink-mute));
  text-align: center;
  font-family: var(--font-serif, serif);
  line-height: 1.65;
  transition: color 0.35s ease, opacity 0.35s ease, transform 0.35s ease, font-size 0.35s ease;
}

.lyric-line.active {
  color: var(--accent);
  font-size: 24px;
  font-weight: 700;
  transform: scale(1.05);
  text-shadow: 0 0 28px color-mix(in srgb, var(--accent) 35%, transparent);
  opacity: 1;
}

.aurora-lyric-fullscreen .lyric-line.active {
  font-size: clamp(26px, 3.2vw, 36px);
}

.aurora-lyric-fullscreen .lyric-line {
  font-size: clamp(17px, 1.6vw, 22px);
  max-width: min(48ch, 94%);
}

/* Readable: all lines scannable — far floor ≥ 0.72 */
.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.near {
  opacity: 0.92;
  font-size: 16px;
}

.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.mid {
  opacity: 0.84;
  font-size: 15px;
}

.aurora-lyric-stage[data-lyric-focus='readable'] .lyric-line.far {
  opacity: 0.78;
  font-size: 14px;
}

/* Stage: near/far hierarchy with far floor ≥ 0.45 */
.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.near {
  opacity: 0.7;
  font-size: 16px;
}

.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.mid {
  opacity: 0.55;
  font-size: 15px;
}

.aurora-lyric-stage[data-lyric-focus='stage'] .lyric-line.far {
  opacity: 0.48;
  font-size: 14px;
}

@media (max-width: 900px) {
  .aurora-lyric-stage {
    grid-template-columns: 1fr;
    grid-template-rows: auto 1fr;
    gap: 16px;
  }

  .aurora-cover {
    width: min(220px, 56vw);
  }
}
</style>

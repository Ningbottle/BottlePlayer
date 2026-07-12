<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { gsap } from 'gsap';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';
import { useLyricFocusStore } from '../../api/lyricFocusStore';
import type { LyricStageModel } from './useLyricStage';

const props = defineProps<{ model: LyricStageModel }>();

defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'user-scroll'): void;
}>();

const coverRef = ref<HTMLElement | null>(null);
const rootRef = ref<HTMLElement | null>(null);
const focus = useLyricFocusStore();

const profile = getMotionProfile('aurora');

function lineClass(idx: number): string {
  const active = props.model.activeIndex;
  if (idx === active) return 'active';
  const diff = Math.abs(idx - active);
  if (diff === 1) return 'near';
  if (diff === 2) return 'mid';
  return 'far';
}

function playEnter(): void {
  if (!rootRef.value || isReducedMotion()) return;
  gsap.fromTo(
    rootRef.value,
    { opacity: 0, y: 20 },
    {
      opacity: 1,
      y: 0,
      duration: profile.pageEnter.duration + 0.08,
      ease: profile.pageEnter.ease,
      onComplete: () => {
        if (rootRef.value) {
          rootRef.value.style.filter = 'none';
          rootRef.value.style.opacity = '';
          rootRef.value.style.transform = '';
        }
      },
    },
  );
  if (coverRef.value) {
    gsap.fromTo(
      coverRef.value,
      { opacity: 0.4, scale: 0.92, x: -24 },
      {
        opacity: 1,
        scale: 1,
        x: 0,
        duration: 0.55,
        ease: 'expo.out',
        delay: 0.04,
      },
    );
  }
}

onMounted(playEnter);

watch(
  () => props.model.currentTrack?.FileHash,
  () => {
    playEnter();
  },
);

watch(() => props.model.fullscreen, (fs) => {
  const cover = coverRef.value;
  if (!cover) return;
  if (isReducedMotion()) {
    if (fs) gsap.set(cover, { width: 320, height: 320 });
    else gsap.set(cover, { clearProps: 'width,height' });
    return;
  }
  if (fs) {
    gsap.to(cover, { width: 320, height: 320, duration: 0.4, ease: 'expo.out' });
  } else {
    gsap.to(cover, {
      width: 280,
      height: 280,
      duration: 0.4,
      ease: 'power2.out',
      clearProps: 'width,height',
    });
  }
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
    <div class="lyric-meta" data-test="lyric-meta" @dblclick="$emit('enter-fullscreen')">
      <div
        class="big-cover aurora-cover"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
      >
        <img :src="model.coverUrl" alt="cover" />
      </div>
      <h2 class="aurora-song-title">{{ model.currentTrack?.SongName }}</h2>
      <p class="aurora-artist">{{ model.currentTrack?.SingerName }}</p>
      <p v-if="!model.fullscreen" class="aurora-fs-hint">双击封面进入全屏</p>
      <button
        v-if="!model.fullscreen"
        type="button"
        class="lyric-focus-toggle"
        data-test="lyric-focus-toggle"
        :aria-pressed="focus.mode.value === 'stage'"
        :aria-label="focus.mode.value === 'readable' ? '切换为舞台渐隐' : '切换为清晰可读'"
        @click="focus.toggle()"
      >
        {{ focus.mode.value === 'readable' ? '清晰' : '舞台' }}
      </button>
    </div>
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
  grid-template-columns: minmax(260px, 34%) minmax(0, 1fr);
  grid-template-rows: 1fr;
  gap: clamp(20px, 3vw, 40px);
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
}

.aurora-cover {
  width: min(280px, 100%);
  height: auto;
  aspect-ratio: 1;
  border-radius: 18px;
  overflow: hidden;
  box-shadow:
    0 18px 48px rgba(0, 0, 0, 0.35),
    0 0 0 1px color-mix(in srgb, var(--accent) 22%, transparent);
  margin-bottom: 18px;
  background: var(--surface-1, var(--paper-2));
}

.aurora-lyric-fullscreen .aurora-cover {
  width: min(46vh, 440px);
  max-width: 100%;
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.aurora-song-title {
  font-size: clamp(18px, 1.6vw, 24px);
  font-weight: 700;
  margin: 0 0 6px;
  text-align: center;
  color: var(--text-primary);
  max-width: 18ch;
}

.aurora-artist {
  font-size: 15px;
  color: var(--text-secondary, var(--ink-soft));
  margin: 0;
  text-align: center;
}

.aurora-fs-hint {
  margin: 12px 0 0;
  font-size: 11px;
  letter-spacing: 0.08em;
  color: var(--text-muted);
  opacity: 0.75;
}

.lyric-focus-toggle {
  margin-top: 14px;
  padding: 6px 14px;
  border-radius: 999px;
  border: 1px solid color-mix(in srgb, var(--accent) 35%, transparent);
  background: color-mix(in srgb, var(--surface-1, var(--paper-2)) 88%, transparent);
  color: var(--text-secondary, var(--ink-soft));
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.04em;
  cursor: pointer;
  transition: color 0.2s ease, border-color 0.2s ease, background 0.2s ease;
}

.lyric-focus-toggle:hover {
  color: var(--text-primary, var(--ink));
  border-color: var(--accent);
}

.lyric-focus-toggle[aria-pressed='true'] {
  color: var(--accent);
  border-color: var(--accent);
}

.lyric-scroll {
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
  align-items: center;
  justify-content: flex-start;
  min-height: 0;
  height: 100%;
  padding: 22% 12px 80px;
  padding-bottom: 80px;
  scrollbar-width: thin;
  scrollbar-gutter: stable;
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
  padding: min(18vh, 120px) clamp(12px, 2vw, 32px) 12vh;
  justify-content: center;
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
    width: 160px;
  }
}
</style>

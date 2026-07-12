<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { gsap } from 'gsap';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';
import type { LyricStageModel } from './useLyricStage';

const props = defineProps<{ model: LyricStageModel }>();

const emit = defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'user-scroll'): void;
  (e: 'seek-line', timeSeconds: number): void;
}>();

function onLineClick(line: { time: number }): void {
  if (!Number.isFinite(line.time) || line.time < 0) return;
  emit('seek-line', line.time);
}

const coverRef = ref<HTMLElement | null>(null);
const rootRef = ref<HTMLElement | null>(null);

const profile = getMotionProfile('newsprint');

onMounted(() => {
  if (rootRef.value && !isReducedMotion()) {
    gsap.fromTo(rootRef.value,
      { opacity: 0, y: 12 },
      { opacity: 1, y: 0, duration: profile.pageEnter.duration, ease: profile.pageEnter.ease },
    );
  }
});

watch(() => props.model.fullscreen, (fs) => {
  const cover = coverRef.value;
  if (!cover) return;
  if (isReducedMotion()) {
    if (fs) gsap.set(cover, { width: 320, height: 320 });
    else gsap.set(cover, { clearProps: 'width,height' });
    return;
  }
  if (fs) {
    gsap.to(cover, { width: 320, height: 320, duration: 0.35, ease: 'power3.out' });
  } else {
    gsap.to(cover, { width: 240, height: 240, duration: 0.35, ease: 'power3.out', clearProps: 'width,height' });
  }
}, { flush: 'post' });
</script>

<template>
  <!-- Fullscreen mode: independent stage template (cover left, lyrics right) -->
  <div v-if="model.fullscreen" class="np-lyric-stage np-lyric-fullscreen" ref="rootRef">
    <div class="lyric-meta" data-test="lyric-meta" @dblclick="$emit('enter-fullscreen')">
      <div
        class="big-cover np-cover"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
      >
        <img :src="model.coverUrl" alt="cover" />
      </div>
      <div class="np-meta-block">
        <div class="np-meta-kicker">NOW PLAYING</div>
        <h2 class="np-song-title">{{ model.currentTrack?.SongName }}</h2>
        <p class="np-artist">{{ model.currentTrack?.SingerName }}</p>
      </div>
    </div>
    <div
      class="lyric-scroll"
      :class="{ paused: !model.autoFollowing }"
      data-test="lyric-scroll"
      :style="{ paddingBottom: '60px' }"
      @wheel.passive="$emit('user-scroll')"
      @touchmove.passive="$emit('user-scroll')"
    >
      <button
        v-for="(line, idx) in model.parsedLyrics"
        :key="idx"
        type="button"
        :id="`lyric-line-${idx}`"
        :data-test="`lyric-line-${idx}`"
        class="np-lyric-line"
        :class="{ active: idx === model.activeIndex }"
        @click="onLineClick(line)"
      >
        <span class="np-line-num">{{ String(idx + 1).padStart(2, '0') }}</span>
        <span class="np-line-text">{{ line.text }}</span>
      </button>
    </div>
  </div>
  <!-- Normal mode: single-column, meta row / scroll row -->
  <div v-else class="np-lyric-stage" ref="rootRef">
    <div class="lyric-meta" data-test="lyric-meta" @dblclick="$emit('enter-fullscreen')">
      <div
        class="big-cover np-cover"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
      >
        <img :src="model.coverUrl" alt="cover" />
      </div>
      <div class="np-meta-block">
        <div class="np-meta-kicker">NOW PLAYING</div>
        <h2 class="np-song-title">{{ model.currentTrack?.SongName }}</h2>
        <p class="np-artist">{{ model.currentTrack?.SingerName }}</p>
      </div>
    </div>
    <div
      class="lyric-scroll"
      :class="{ paused: !model.autoFollowing }"
      data-test="lyric-scroll"
      :style="{ paddingBottom: '60px' }"
      @wheel.passive="$emit('user-scroll')"
      @touchmove.passive="$emit('user-scroll')"
    >
      <button
        v-for="(line, idx) in model.parsedLyrics"
        :key="idx"
        type="button"
        :id="`lyric-line-${idx}`"
        :data-test="`lyric-line-${idx}`"
        class="np-lyric-line"
        :class="{ active: idx === model.activeIndex }"
        @click="onLineClick(line)"
      >
        <span class="np-line-num">{{ String(idx + 1).padStart(2, '0') }}</span>
        <span class="np-line-text">{{ line.text }}</span>
      </button>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'NewsprintLyricStage' };
</script>

<style scoped>
.np-lyric-stage {
  display: grid;
  grid-template-rows: auto 1fr;
  grid-template-columns: 1fr;
  gap: 20px;
  height: 100%;
  min-height: 0;
  padding: 20px 16px;
}

.np-lyric-fullscreen {
  grid-template-rows: 1fr;
  grid-template-columns: 380px 1fr;
  gap: 40px;
  padding: 32px 48px;
}

.lyric-meta {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}

.np-cover {
  width: 240px;
  height: 240px;
  overflow: hidden;
  border: 2px solid var(--ink);
  box-shadow: 4px 4px 0 var(--ink-soft);
  background: var(--paper-2);
}

.np-lyric-fullscreen .np-cover {
  width: 320px;
  height: 320px;
}

.np-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  filter: saturate(0.75);
}

.np-meta-block {
  text-align: center;
}

.np-meta-kicker {
  font-family: var(--font-serif, serif);
  font-style: italic;
  font-size: 11px;
  letter-spacing: 0.14em;
  color: var(--ink-mute);
  text-transform: uppercase;
  margin-bottom: 4px;
}

.np-song-title {
  font-size: 20px;
  font-weight: 800;
  margin: 0;
  font-family: var(--font-serif, serif);
}

.np-artist {
  font-size: 14px;
  color: var(--ink-soft);
  margin: 2px 0 0;
  font-style: italic;
}

.lyric-scroll {
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  gap: 4px;
  justify-content: flex-start;
  padding: min(18vh, 120px) 0 min(18vh, 100px);
  mask-image: linear-gradient(to bottom, transparent, white 12%, white 88%, transparent);
  -webkit-mask-image: linear-gradient(to bottom, transparent, white 12%, white 88%, transparent);
  scrollbar-width: thin;
}

.np-lyric-fullscreen .lyric-scroll {
  padding: min(22vh, 160px) 0 min(22vh, 140px);
  scrollbar-width: none;
}
.np-lyric-fullscreen .lyric-scroll::-webkit-scrollbar {
  width: 0;
  display: none;
}

.np-lyric-line {
  display: grid;
  grid-template-columns: 36px 1fr;
  gap: 12px;
  padding: 8px 12px;
  border: 0;
  border-bottom: 1px dotted var(--rule-soft, var(--rule));
  background: transparent;
  width: 100%;
  text-align: left;
  cursor: pointer;
  font-family: var(--font-serif, serif);
  font-size: 15px;
  color: var(--ink-mute);
  transition: background 0.2s ease, color 0.2s ease;
}

.np-lyric-fullscreen .np-lyric-line {
  font-size: 17px;
}

.np-lyric-line.active {
  background: var(--surface-1, var(--paper-2));
  color: var(--ink);
  font-weight: 700;
  border-bottom: 1px solid var(--ink);
}

.np-lyric-fullscreen .np-lyric-line.active {
  font-size: 20px;
}

.np-line-num {
  font-size: 13px;
  font-weight: 600;
  color: var(--ink-faint, var(--ink-mute));
  text-align: right;
  font-style: italic;
}

.np-lyric-line.active .np-line-num {
  color: var(--accent);
}

.np-line-text {
  line-height: 1.5;
}
</style>

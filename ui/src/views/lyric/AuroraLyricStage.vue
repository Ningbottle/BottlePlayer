<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { gsap } from 'gsap';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';
import type { LyricStageModel } from './useLyricStage';

const props = defineProps<{ model: LyricStageModel }>();

defineEmits<{
  (e: 'enter-fullscreen'): void;
  (e: 'user-scroll'): void;
}>();

const coverRef = ref<HTMLElement | null>(null);
const rootRef = ref<HTMLElement | null>(null);

const profile = getMotionProfile('aurora');

function lineClass(idx: number): string {
  const active = props.model.activeIndex;
  if (idx === active) return 'active';
  const diff = Math.abs(idx - active);
  if (diff === 1) return 'near';
  if (diff === 2) return 'mid';
  return 'far';
}

onMounted(() => {
  if (rootRef.value && !isReducedMotion()) {
    gsap.fromTo(rootRef.value,
      { opacity: 0, y: 16 },
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
    gsap.to(cover, { width: 320, height: 320, duration: 0.4, ease: 'expo.out' });
  } else {
    gsap.to(cover, { width: 240, height: 240, duration: 0.4, ease: 'power2.out', clearProps: 'width,height' });
  }
});
</script>

<template>
  <div class="aurora-lyric-stage" ref="rootRef">
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
    </div>
    <div
      class="lyric-scroll"
      :class="{ paused: !model.autoFollowing }"
      data-test="lyric-scroll"
      :style="{ paddingBottom: '60px' }"
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
  grid-template-rows: auto 1fr;
  grid-template-columns: 260px 1fr;
  gap: 32px;
  height: 100%;
  padding: 20px 16px;
}

.lyric-meta {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.aurora-cover {
  width: 240px;
  height: 240px;
  border-radius: 16px;
  overflow: hidden;
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.18);
  margin-bottom: 20px;
  background: var(--surface-1, var(--paper-2));
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  transition: opacity 0.2s ease;
}

.aurora-song-title {
  font-size: 22px;
  font-weight: 700;
  margin: 0 0 6px;
  text-align: center;
}

.aurora-artist {
  font-size: 15px;
  color: var(--ink-soft);
  margin: 0;
}

.lyric-scroll {
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 14px;
  align-items: center;
  padding: 30% 0 60px;
  mask-image: linear-gradient(to bottom, transparent, white 15%, white 85%, transparent);
  -webkit-mask-image: linear-gradient(to bottom, transparent, white 15%, white 85%, transparent);
  scrollbar-width: thin;
}

.lyric-line {
  width: 100%;
  font-size: 16px;
  color: var(--ink-mute);
  text-align: center;
  font-family: var(--font-serif, serif);
  line-height: 1.6;
  transition: color 0.3s ease, opacity 0.3s ease, transform 0.3s ease, font-size 0.3s ease;
}

.lyric-line.active {
  color: var(--accent);
  font-size: 22px;
  font-weight: 700;
  transform: scale(1.06);
}

.lyric-line.near {
  opacity: 0.6;
  font-size: 15px;
}

.lyric-line.mid {
  opacity: 0.35;
  font-size: 14px;
}

.lyric-line.far {
  opacity: 0.18;
  font-size: 13px;
}
</style>

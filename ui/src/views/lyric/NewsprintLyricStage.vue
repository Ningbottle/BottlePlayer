<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { gsap } from 'gsap';
import { Disc3, Maximize2, Pause, Play } from '@lucide/vue';
import { isReducedMotion } from '../../api/motion';
import { getMotionProfile } from '../../api/motionProfiles';
import { togglePlay as storeTogglePlay } from '../../api/playerStore';
import PlayerProgress from '../../components/player/PlayerProgress.vue';
import type { LyricStageModel } from './useLyricStage';
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

onBeforeUnmount(autoHideControls.dispose);
</script>

<template>
  <div
    ref="rootRef"
    class="np-lyric-stage"
    :class="{ 'np-lyric-fullscreen': model.fullscreen }"
    data-test="newsprint-lyric-stage"
    @pointermove="autoHideControls.onPointerMove"
    @focusin="autoHideControls.onFocusIn"
    @focusout="autoHideControls.onFocusOut"
  >
    <div
      class="lyric-meta np-lyric-meta-column"
      data-test="lyric-meta-column"
      @dblclick="!model.fullscreen && $emit('enter-fullscreen')"
    >
      <div
        class="big-cover np-cover"
        ref="coverRef"
        data-test="lyric-cover"
        :style="{ aspectRatio: '1' }"
      >
        <img v-if="model.coverUrl" :src="model.coverUrl" alt="cover" />
        <Disc3
          v-else
          class="np-cover-placeholder"
          data-test="lyric-cover-placeholder"
          data-icon-family="lucide"
          :size="72"
          :stroke-width="1.15"
          aria-hidden="true"
        />
      </div>
      <button
        v-if="!model.fullscreen"
        type="button"
        class="np-lyric-enter-fullscreen"
        data-test="lyric-enter-fullscreen"
        aria-label="进入全屏歌词"
        title="进入全屏歌词"
        @click.stop="$emit('enter-fullscreen')"
      >
        <Maximize2 :size="15" :stroke-width="1.75" aria-hidden="true" />
      </button>
      <div class="np-meta-block">
        <div class="np-meta-kicker">正在播放 · NOW PLAYING</div>
        <h2 class="np-song-title">{{ model.currentTrack?.SongName }}</h2>
        <p class="np-artist">{{ model.currentTrack?.SingerName }}</p>
        <small class="np-album">{{ model.currentTrack?.AlbumName || '未知专辑' }}</small>
      </div>
    </div>
    <div
      class="lyric-content-column np-lyric-content-column"
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
          <span class="np-line-text">{{ line.text }}</span>
        </button>
      </div>
      <slot name="footer" />
      <div
        v-if="model.fullscreen && model.duration > 0"
        class="np-fs-controls"
        :class="{ 'controls-visible': controlsVisible }"
        data-test="newsprint-fs-controls"
        :data-visible="String(controlsVisible)"
        data-visual-weight="subtle"
      >
        <button
          type="button"
          class="np-fs-play"
          :data-test="model.isPlaying ? 'newsprint-fs-pause' : 'newsprint-fs-play'"
          :aria-label="model.isPlaying ? '暂停' : '播放'"
          :title="model.isPlaying ? '暂停' : '播放'"
          @click="storeTogglePlay"
        >
          <Pause v-if="model.isPlaying" :size="15" :stroke-width="1.8" aria-hidden="true" />
          <Play v-else :size="15" :stroke-width="1.8" aria-hidden="true" />
        </button>
        <PlayerProgress
          :current-time="model.currentTime"
          :duration="model.duration"
          @seek="(s: number) => emit('seek', s)"
        />
      </div>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'NewsprintLyricStage' };
</script>

<style scoped>
.np-lyric-stage {
  display: grid;
  grid-template-rows: minmax(0, 1fr);
  grid-template-columns: minmax(180px, 32%) minmax(0, 1fr);
  gap: clamp(18px, 3vw, 40px);
  height: 100%;
  min-height: 0;
  padding: 20px clamp(16px, 3vw, 40px);
  box-sizing: border-box;
  overflow: hidden;
}

.np-lyric-fullscreen {
  position: relative;
  grid-template-columns: 380px 1fr;
  gap: 40px;
  padding: 32px 48px;
}

.lyric-meta {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;
  min-width: 0;
  overflow: hidden;
}

.np-cover {
  position: relative;
  width: min(240px, 100%);
  height: auto;
  aspect-ratio: 1 / 1;
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
  min-width: 0;
  max-width: 100%;
}

.np-cover-placeholder {
  position: absolute;
  top: 50%;
  left: 50%;
  width: 28%;
  height: 28%;
  color: var(--ink-soft);
  opacity: 0.72;
  transform: translate(-50%, -50%);
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

.np-album {
  display: block;
  margin-top: 5px;
  color: var(--ink-mute);
  font-family: var(--font-serif, serif);
  font-size: 11px;
  font-style: italic;
}

.np-lyric-enter-fullscreen {
  width: 28px;
  height: 24px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 1px solid var(--rule-soft, var(--rule));
  border-radius: 2px;
  background: transparent;
  color: var(--ink-mute);
  cursor: pointer;
  line-height: 0;
  transition: color 0.15s ease, border-color 0.15s ease;
}

.np-lyric-enter-fullscreen:hover,
.np-lyric-enter-fullscreen:focus-visible {
  color: var(--ink);
  border-color: var(--ink-soft);
}

.np-lyric-enter-fullscreen:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

.np-lyric-content-column {
  min-width: 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
}

.np-fs-controls {
  flex: 0 0 auto;
  width: min(430px, 100%);
  align-self: flex-end;
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 8px 0 48px;
  padding: 4px 8px;
  border-top: 1px solid var(--rule-soft, var(--rule));
  border-bottom: 1px solid var(--rule-soft, var(--rule));
  background: color-mix(in srgb, var(--paper-2) 72%, transparent);
  box-sizing: border-box;
  opacity: 0;
  pointer-events: none;
  transform: translateY(4px);
  transition: opacity 0.16s ease, transform 0.16s ease;
}

.np-lyric-fullscreen .np-fs-controls {
  position: absolute;
  left: 48px;
  bottom: 28px;
  width: 430px;
  max-width: calc(100% - 96px);
  align-self: auto;
  margin: 0;
  z-index: 4;
}

.np-fs-controls.controls-visible,
.np-fs-controls:focus-within {
  opacity: 0.86;
  pointer-events: auto;
  transform: translateY(0);
}

.np-fs-controls:hover,
.np-fs-controls:focus-within {
  opacity: 1;
}

.np-fs-play {
  flex: 0 0 auto;
  width: 26px;
  height: 26px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 1px solid var(--rule-soft, var(--rule));
  border-radius: 2px;
  background: transparent;
  color: var(--ink-soft);
  cursor: pointer;
  line-height: 0;
}

.np-fs-play:hover,
.np-fs-play:focus-visible {
  color: var(--ink);
  border-color: var(--ink-soft);
}

.np-fs-play:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

.np-fs-controls :deep(.progress-time) {
  color: var(--ink-mute);
}

@media (prefers-reduced-motion: reduce) {
  .np-fs-controls {
    transform: none;
    transition: none;
  }
}

.lyric-scroll {
  flex: 1 1 0;
  min-width: 0;
  min-height: 0;
  overflow-y: auto;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  gap: 4px;
  justify-content: flex-start;
  padding: min(18vh, 120px) 0 min(18vh, 100px);
  mask-image: linear-gradient(to bottom, transparent, white 12%, white 88%, transparent);
  -webkit-mask-image: linear-gradient(to bottom, transparent, white 12%, white 88%, transparent);
  scrollbar-width: none;
  -ms-overflow-style: none;
}

.lyric-scroll::-webkit-scrollbar {
  width: 0;
  height: 0;
  display: none;
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
  display: block;
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

@media (max-width: 900px) {
  .np-lyric-stage,
  .np-lyric-fullscreen {
    grid-template-columns: minmax(140px, 34%) minmax(0, 1fr);
    gap: 14px;
    padding: 14px 12px;
  }

  .np-cover,
  .np-lyric-fullscreen .np-cover {
    width: min(30vw, 210px, 30vh);
    height: auto;
  }

  .np-lyric-fullscreen .np-fs-controls {
    left: 12px;
    bottom: 16px;
    width: min(360px, calc(100% - 24px));
    max-width: none;
  }

  .np-song-title {
    font-size: 17px;
  }

  .np-lyric-line,
  .np-lyric-fullscreen .np-lyric-line {
    grid-template-columns: 28px minmax(0, 1fr);
    gap: 8px;
    padding-inline: 6px;
    font-size: 14px;
  }
}
</style>

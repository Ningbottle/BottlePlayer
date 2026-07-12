<script setup lang="ts">
import { computed, ref, watch, nextTick, onBeforeUnmount } from 'vue';
import { gsap } from 'gsap';
import { useThemeStore } from '../api/themeStore';
import { useLyricStage } from './lyric/useLyricStage';
import AuroraLyricStage from './lyric/AuroraLyricStage.vue';
import NewsprintLyricStage from './lyric/NewsprintLyricStage.vue';
import LyricFollowFooter from './lyric/LyricFollowFooter.vue';
import { isReducedMotion } from '../api/motion';

const props = defineProps<{
  isQueueOpen?: boolean;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string): void;
}>();

const themeStore = useThemeStore();
const { model, commands } = useLyricStage();

const stageComponent = computed(() =>
  themeStore.skinId.value === 'aurora' ? AuroraLyricStage : NewsprintLyricStage,
);

const isAurora = computed(() => themeStore.skinId.value === 'aurora');
const curtainRef = ref<HTMLElement | null>(null);
let curtainTween: { kill: () => void } | null = null;

/**
 * Curtain drop: fabric throws down from the top, then lifts away —
 * page-level theatrical open (not lyric-line motion).
 */
function playCurtainDrop(): void {
  if (!isAurora.value || isReducedMotion()) return;
  const el = curtainRef.value;
  if (!el) return;

  curtainTween?.kill();
  gsap.killTweensOf(el);
  gsap.set(el, {
    opacity: 1,
    yPercent: -105,
    scaleY: 1,
    transformOrigin: '50% 0%',
    display: 'block',
  });

  const tl = gsap.timeline({
    onComplete: () => {
      gsap.set(el, { display: 'none', clearProps: 'transform,opacity' });
      curtainTween = null;
    },
  });
  // Throw down
  tl.to(el, {
    yPercent: 0,
    duration: 0.48,
    ease: 'power2.in',
  });
  // Settle / soft overshoot
  tl.to(el, {
    yPercent: 2,
    duration: 0.14,
    ease: 'power1.out',
  });
  // Lift / peel up like a raised curtain
  tl.to(el, {
    yPercent: -108,
    duration: 0.72,
    ease: 'power3.inOut',
  });
  curtainTween = tl;
}

watch(
  () => [model.value.currentTrack?.FileHash, model.value.loading, isAurora.value] as const,
  async ([hash, loading, aurora]) => {
    if (!aurora || !hash || loading) return;
    await nextTick();
    playCurtainDrop();
  },
  { flush: 'post' },
);

onBeforeUnmount(() => {
  curtainTween?.kill();
  curtainTween = null;
});
</script>

<template>
  <div class="list-view lyric-view" :class="{ 'is-aurora': isAurora }">
    <!-- Aurora page curtain (throws down, then lifts) -->
    <div
      v-if="isAurora"
      ref="curtainRef"
      class="lyric-curtain"
      data-test="lyric-curtain"
      aria-hidden="true"
    />

    <!-- Empty/No track state -->
    <div v-if="!model.currentTrack" class="lyric-empty-state" data-test="lyric-empty-state">
      <p class="lyric-empty-kicker">LYRICS</p>
      <h1>选择一首歌，歌词会在这里展开</h1>
      <p>从首页的每日推荐或搜索结果开始播放。</p>
      <div class="lyric-empty-actions">
        <button type="button" data-test="lyric-empty-home" @click="emit('navigate', 'home')">回到首页</button>
        <button type="button" data-test="lyric-empty-search" @click="emit('navigate', 'search')">搜索歌曲</button>
      </div>
    </div>

    <!-- Loading -->
    <div v-else-if="model.loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      译稿编撰中…
    </div>

    <!-- Lyric layout: three-row grid (meta / scroll / footer) -->
    <div
      v-else
      class="lyric-view-grid"
      :class="{ 'queue-open': props.isQueueOpen && !model.fullscreen, fullscreen: model.fullscreen }"
      data-test="lyric-grid"
    >
      <component
        :is="stageComponent"
        :model="model"
        class="lyric-stage-slot"
        @enter-fullscreen="commands.enterFullscreen"
        @user-scroll="commands.onUserScroll"
      />
      <LyricFollowFooter
        v-if="!model.fullscreen"
        :auto-following="model.autoFollowing"
        @resume="commands.resumeFollow"
      />
    </div>
  </div>
</template>

<style scoped>
.lyric-view {
  position: relative;
  min-width: 0;
  max-width: 100%;
  width: 100%;
  box-sizing: border-box;
  height: 100%;
  overflow: hidden;
}

/* Fabric curtain — throws from top, then peels away */
.lyric-curtain {
  position: absolute;
  inset: 0;
  z-index: 30;
  pointer-events: none;
  display: none;
  background:
    linear-gradient(
      180deg,
      color-mix(in srgb, var(--accent) 28%, #050a08) 0%,
      color-mix(in srgb, var(--surface-elevated, #0d1213) 92%, var(--accent) 8%) 42%,
      color-mix(in srgb, var(--app-bg, #040607) 88%, var(--accent) 6%) 100%
    );
  box-shadow:
    0 12px 40px rgba(0, 0, 0, 0.45),
    inset 0 -1px 0 color-mix(in srgb, var(--accent) 22%, transparent);
}

.lyric-curtain::after {
  content: '';
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  height: 28%;
  background: linear-gradient(
    180deg,
    transparent,
    color-mix(in srgb, #000 35%, transparent)
  );
  opacity: 0.55;
}

.lyric-view-grid {
  display: grid;
  grid-template-rows: 1fr auto;
  height: calc(100vh - 160px);
  min-height: 420px;
  transition: padding-right 0.2s ease;
  min-width: 0;
  width: 100%;
}

.lyric-view-grid.queue-open {
  padding-right: 340px;
}

.lyric-view-grid.fullscreen {
  grid-template-rows: 1fr;
  height: 100vh;
  min-height: 100vh;
  width: 100%;
  padding: 0;
}

.lyric-stage-slot {
  grid-row: 1;
  min-height: 0;
  min-width: 0;
}

.lyric-view-grid.fullscreen .lyric-stage-slot {
  grid-row: 1 / -1;
}

.lyric-empty-state {
  min-height: min(520px, calc(100vh - 180px));
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  justify-content: center;
  gap: 12px;
  padding: clamp(32px, 7vw, 96px);
  color: var(--text-secondary);
}

.lyric-empty-state p,
.lyric-empty-state h1 {
  margin: 0;
}

.lyric-empty-kicker {
  color: var(--accent);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.15em;
}

.lyric-empty-state h1 {
  max-width: 14ch;
  color: var(--text-primary);
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: clamp(28px, 4vw, 48px);
  line-height: 1.15;
}

.lyric-empty-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin-top: 8px;
}

.lyric-empty-state button {
  padding: 10px 18px;
  border-radius: 999px;
  border: 1px solid color-mix(in srgb, var(--accent) 40%, transparent);
  background: color-mix(in srgb, var(--accent) 14%, transparent);
  color: var(--text-primary);
  cursor: pointer;
  font: inherit;
}

.spinner {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  min-height: 240px;
  color: var(--text-muted);
}

.spinner svg {
  width: 28px;
  height: 28px;
  animation: spin 0.9s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}
</style>

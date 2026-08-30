<script setup lang="ts">
import { computed } from 'vue';
import { useThemeStore } from '../app/appearance/themeStore';
import { useLyricStage } from './lyric/useLyricStage';
import AuroraLyricStage from './lyric/AuroraLyricStage.vue';
import NewsprintLyricStage from './lyric/NewsprintLyricStage.vue';
import LyricFollowFooter from './lyric/LyricFollowFooter.vue';

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
</script>

<template>
  <div class="list-view lyric-view">
    <!--
      No separate page curtain — stage enter lives only in AuroraLyricStage
      (one coordinated open). Extra overlay felt disconnected and double-fired.
    -->
    <div v-if="!model.currentTrack" class="lyric-empty-state" data-test="lyric-empty-state">
      <p class="lyric-empty-kicker">LYRICS</p>
      <h1>选择一首歌，歌词会在这里展开</h1>
      <p>从首页的每日推荐或搜索结果开始播放。</p>
      <div class="lyric-empty-actions">
        <button type="button" data-test="lyric-empty-home" @click="emit('navigate', 'home')">回到首页</button>
        <button type="button" data-test="lyric-empty-search" @click="emit('navigate', 'search')">搜索歌曲</button>
      </div>
    </div>

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
        @exit-fullscreen="commands.exitFullscreen"
        @user-scroll="commands.onUserScroll"
        @seek-line="commands.seekToLine"
        @seek="commands.seekToLine"
      >
        <template #loading>
          <div class="spinner" data-test="lyric-loading" role="status" aria-live="polite">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
              <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
              <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
            </svg>
            译稿编撰中…
          </div>
        </template>
        <template #error>
          <div class="lyric-error-state" data-test="lyric-error" role="alert">
            <strong>歌词暂时无法加载</strong>
            <span>连接恢复后可以重新获取当前歌曲的歌词。</span>
            <button type="button" data-test="lyric-retry" @click="commands.retryLyrics">重试歌词</button>
          </div>
        </template>
        <template v-if="!model.fullscreen && !model.loading" #footer>
          <LyricFollowFooter
            :auto-following="model.autoFollowing"
            @resume="commands.resumeFollow"
          />
        </template>
      </component>
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
}

.lyric-view-grid {
  display: grid;
  grid-template-rows: minmax(0, 1fr);
  height: calc(100vh - 160px);
  min-height: 420px;
  transition: padding-right 0.2s ease;
  min-width: 0;
  width: 100%;
}

.lyric-error-state {
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 24px;
  text-align: center;
  color: var(--text-secondary, var(--ink-soft));
}

.lyric-error-state strong {
  color: var(--text-primary, var(--ink));
  font-size: 16px;
}

.lyric-error-state span {
  max-width: 32ch;
  font-size: 12px;
  line-height: 1.6;
}

.lyric-error-state button {
  margin-top: 4px;
  padding: 7px 14px;
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  border-radius: 6px;
  background: transparent;
  color: var(--text-primary, var(--ink));
  cursor: pointer;
  font: inherit;
}

.lyric-error-state button:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

.lyric-view-grid.queue-open {
  padding-right: 340px;
}

.lyric-view-grid.queue-open .aurora-cover {
  width: min(26vw, 36vh, 280px) !important;
}

.lyric-view-grid.queue-open .lyric-line {
  font-size: 17px !important;
}

.lyric-view-grid.queue-open .lyric-line.active {
  font-size: 24px !important;
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
  flex: 1 1 0;
  min-height: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
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

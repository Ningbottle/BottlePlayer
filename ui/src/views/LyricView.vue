<script setup lang="ts">
import { computed } from 'vue';
import { useThemeStore } from '../api/themeStore';
import { useLyricStage } from './lyric/useLyricStage';
import AuroraLyricStage from './lyric/AuroraLyricStage.vue';
import NewsprintLyricStage from './lyric/NewsprintLyricStage.vue';
import LyricFollowFooter from './lyric/LyricFollowFooter.vue';

defineProps<{
  isQueueOpen?: boolean;
  isDrawerOpen?: boolean;
}>();

const themeStore = useThemeStore();
const { model, commands } = useLyricStage();

const stageComponent = computed(() =>
  themeStore.skinId.value === 'aurora' ? AuroraLyricStage : NewsprintLyricStage,
);
</script>

<template>
  <div class="list-view lyric-view">
    <!-- Empty/No track state -->
    <div v-if="!model.currentTrack" class="spinner">
      未选择正在播放的曲目。请在首页或搜索页点播。
    </div>

    <!-- Loading -->
    <div v-else-if="model.loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      译稿编撰中…
    </div>

    <!-- Lyric layout: three-row grid -->
    <div v-else class="lyric-view-grid" data-test="lyric-grid">
      <component
        :is="stageComponent"
        :model="model"
        @enter-fullscreen="commands.enterFullscreen"
        @user-scroll="commands.onUserScroll"
      />
      <LyricFollowFooter
        :auto-following="model.autoFollowing"
        @resume="commands.resumeFollow"
      />
    </div>
  </div>
</template>

<style scoped>
.lyric-view-grid {
  display: grid;
  grid-template-rows: 1fr auto;
  height: calc(100vh - 140px);
}
</style>

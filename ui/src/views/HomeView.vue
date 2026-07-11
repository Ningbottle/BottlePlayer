<script setup lang="ts">
import { onMounted, onActivated, onDeactivated, nextTick, ref, computed } from 'vue';
import { playTrack, playPersonalFm } from '../api/playerStore';
import type { Track } from '../api/normalizer';
import { useHomeFeedStore } from '../api/homeFeedStore';
import { useThemeStore } from '../api/themeStore';
import { useHomeViewModel } from './home/homeViewModel';
import AuroraHome from './home/AuroraHome.vue';
import NewsprintHome from './home/NewsprintHome.vue';

defineOptions({ name: 'HomeView' });

const scrollPositions: Record<string, number> = {};
const themeStore = useThemeStore();
const rootEl = ref<HTMLElement | null>(null);

const emit = defineEmits<{
  (e: 'navigate', view: string, params?: any): void;
}>();

const homeFeed = useHomeFeedStore();
const viewModel = useHomeViewModel();

const homeComponent = computed(() =>
  themeStore.skinId.value === 'aurora' ? AuroraHome : NewsprintHome,
);

onMounted(() => {
  homeFeed.ensureLoaded();
});

function findScrollContainer(): HTMLElement | null {
  let el: HTMLElement | null = rootEl.value;
  while (el) {
    if (el.classList.contains('scroll')) return el;
    el = el.parentElement;
  }
  return null;
}

let scrollContainer: HTMLElement | null = null;

onActivated(() => {
  scrollContainer = findScrollContainer();
  const skinKey = `home:${themeStore.skinId.value}`;
  const saved = scrollPositions[skinKey];
  if (saved != null && scrollContainer) {
    nextTick(() => {
      if (scrollContainer) scrollContainer.scrollTop = saved;
    });
  }
});

onDeactivated(() => {
  if (scrollContainer) {
    scrollPositions[`home:${themeStore.skinId.value}`] = scrollContainer.scrollTop;
  }
});

function onPlayTrack(track: Track) {
  const idx = homeFeed.daily.items.findIndex(s => s.FileHash === track.FileHash);
  if (idx >= 0) {
    playPersonalFm(homeFeed.daily.items, idx);
  } else {
    playTrack(track);
  }
}

function onRefresh() {
  homeFeed.refresh();
}

function onNavigate(view: string, params?: any) {
  emit('navigate', view, params);
}
</script>

<template>
  <div ref="rootEl" class="list-view">
    <component
      :is="homeComponent"
      :model="viewModel"
      @play-track="onPlayTrack"
      @refresh="onRefresh"
      @navigate="onNavigate"
    />
  </div>
</template>

<style scoped>
</style>

<script setup lang="ts">
import { onMounted, onActivated, onDeactivated, nextTick, ref, computed } from 'vue';
import { playTrack, playPersonalFm, clearQueue } from '../api/playerStore';
import type { Track } from '../api/normalizer';
import { useHomeFeedStore } from '../api/homeFeedStore';
import { useThemeStore } from '../api/themeStore';
import { nextHomeEnterMode, type HomeEnterMode } from '../api/homeEnterSession';
import { useHomeViewModel } from './home/homeViewModel';
import AuroraHome from './home/AuroraHome.vue';
import NewsprintHome from './home/NewsprintHome.vue';

defineOptions({ name: 'HomeView' });

const scrollPositions: Record<string, number> = {};
const themeStore = useThemeStore();
const rootEl = ref<HTMLElement | null>(null);

/** Aurora home enter: only advanced in onActivated (KeepAlive-safe). */
const enterMode = ref<HomeEnterMode | 'none'>('none');
const enterNonce = ref(0);

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

// Use ONLY onActivated for KeepAlive children (fires on first insert AND later activates).
// Do NOT also set cold in onMounted and return in onActivated.
onActivated(() => {
  scrollContainer = findScrollContainer();
  const skinKey = `home:${themeStore.skinId.value}`;
  const saved = scrollPositions[skinKey];
  if (saved != null && scrollContainer) {
    nextTick(() => {
      if (scrollContainer) scrollContainer.scrollTop = saved;
    });
  }

  if (themeStore.skinId.value === 'aurora') {
    enterMode.value = nextHomeEnterMode();
    enterNonce.value += 1;
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

function onPlayQueueTrack(track: Track) {
  playTrack(track);
}

function onRefresh() {
  homeFeed.refresh();
}

function onNavigate(view: string, params?: any) {
  emit('navigate', view, params);
}

function onClearQueue() {
  clearQueue();
}
</script>

<template>
  <div ref="rootEl" class="list-view home-view-root">
    <Transition name="skin-crossfade" mode="out-in">
      <component
        :is="homeComponent"
        :model="viewModel"
        :enter-mode="enterMode"
        :enter-nonce="enterNonce"
        @play-track="onPlayTrack"
        @play-queue-track="onPlayQueueTrack"
        @refresh="onRefresh"
        @navigate="onNavigate"
        @clear-queue="onClearQueue"
      />
    </Transition>
  </div>
</template>

<style scoped>
.skin-crossfade-enter-active,
.skin-crossfade-leave-active {
  transition: opacity 0.15s ease;
}

.skin-crossfade-enter-from,
.skin-crossfade-leave-to {
  opacity: 0;
}
</style>

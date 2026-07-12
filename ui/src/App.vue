<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';

import Sidebar from './components/Sidebar.vue';
import Topbar from './components/Topbar.vue';
import PlayerBar from './components/PlayerBar.vue';
import Drawer from './components/Drawer.vue';
import QueuePanel from './components/QueuePanel.vue';
import AuroraShell from './components/shell/AuroraShell.vue';
import NewsprintShell from './components/shell/NewsprintShell.vue';
import FullscreenWindowControls from './components/shell/FullscreenWindowControls.vue';

import { initPlayer, initPlayerBackend } from './api/playerStore';
import { checkLoginStatus } from './api/userStore';
import { ping } from './api/backend';
import { invoke } from '@tauri-apps/api/core';
import { lyricFullscreen, setLyricFullscreen } from './api/lyricFullscreen';
import { transitionEnter, transitionLeave } from './api/motion';
import { resolveViewDescriptor, type HistoryEntry, type ViewDescriptor } from './api/viewRegistry';
import { useThemeStore } from './api/themeStore';

const themeStore = useThemeStore();
const currentShell = computed(() => themeStore.skinId.value === 'aurora' ? AuroraShell : NewsprintShell);
/** Aurora: simultaneous enter/leave (overlap). Newsprint: serial out-in. */
const pageTransitionMode = computed<'out-in' | undefined>(() =>
  themeStore.skinId.value === 'aurora' ? undefined : 'out-in',
);
const isAuroraOverlap = computed(() => themeStore.skinId.value === 'aurora');

const currentView = ref('home');
const searchQuery = ref('');
const playlistId = ref('');
const playlistName = ref('');
const tweaksCollapsed = ref(true);
const isQueueOpen = ref(false);
const networkDegraded = ref(false);
let networkInterval: ReturnType<typeof setInterval> | null = null;

async function updateNetworkBanner() {
  try {
    await ping();
    networkDegraded.value = false;
  } catch (e) {
    networkDegraded.value = true;
  }
}

// Memory usage tracking
const memoryUsage = ref('Working Set: -- / 220 MB');
let memInterval: any = null;

async function fetchMemoryUsage() {
  try {
    const bytes = await invoke<number>('get_memory_usage');
    const mb = bytes / (1024 * 1024);
    memoryUsage.value = `Working Set: ${mb.toFixed(1)} / 220 MB`;
  } catch (e) {
    // Graceful fallback
  }
}

// Navigation History Stack
const historyStack = ref<HistoryEntry[]>([{ view: 'home' }]);
const historyIndex = ref(0);

const currentEntry = computed<HistoryEntry>(() => historyStack.value[historyIndex.value]);
const currentDescriptor = computed<ViewDescriptor>(() => resolveViewDescriptor(currentEntry.value));

const viewProps = computed<Record<string, unknown>>(() => {
  switch (currentEntry.value.view) {
    case 'search':
      return { query: searchQuery.value };
    case 'playlist':
      return { playlistId: playlistId.value, playlistName: playlistName.value };
    case 'lyric':
      return { isQueueOpen: isQueueOpen.value };
    default:
      return {};
  }
});

function pushHistory(entry: HistoryEntry) {
  historyStack.value.splice(historyIndex.value + 1);
  historyStack.value.push(entry);
  historyIndex.value = historyStack.value.length - 1;
}

function applyHistoryEntry(entry: HistoryEntry) {
  currentView.value = entry.view;
  if (entry.view === 'playlist') {
    playlistId.value = entry.playlistId || '';
    playlistName.value = entry.playlistName || '';
  } else if (entry.view === 'search') {
    searchQuery.value = entry.searchQuery || '';
  }
}

function handleNavigate(view: string, params?: any) {
  const entry: HistoryEntry = { view: view as HistoryEntry['view'] };
  if (view === 'playlist' && params) {
    entry.playlistId = params.id;
    entry.playlistName = params.name;
  }
  applyHistoryEntry(entry);
  pushHistory(entry);
}

function handleSearch(query: string) {
  if (query.trim()) {
    const entry: HistoryEntry = { view: 'search', searchQuery: query };
    applyHistoryEntry(entry);
    pushHistory(entry);
  }
}

function goBack() {
  if (historyIndex.value > 0) {
    historyIndex.value--;
    applyHistoryEntry(historyStack.value[historyIndex.value]);
  }
}

function goForward() {
  if (historyIndex.value < historyStack.value.length - 1) {
    historyIndex.value++;
    applyHistoryEntry(historyStack.value[historyIndex.value]);
  }
}

onMounted(() => {
  // Clear any stuck inline styles from interrupted page transitions
  document.querySelectorAll('.scroll > *, .list-view, .aurora-home, .np-home').forEach((node) => {
    const el = node as HTMLElement;
    el.style.opacity = '';
    el.style.filter = '';
    el.style.transform = '';
  });
  // Don't boot into a broken fullscreen shell with zero chrome rows
  setLyricFullscreen(false);

  // Initialize HTML5 Audio element and reactive player events
  initPlayer();
  // Initialize native playback backend (falls back to HTML5)
  initPlayerBackend();
  // Fetch initial login status
  checkLoginStatus();

  // Start memory polling
  fetchMemoryUsage();
  memInterval = setInterval(fetchMemoryUsage, 2500);
  updateNetworkBanner();
  networkInterval = setInterval(updateNetworkBanner, 5_000);
});

onUnmounted(() => {
  if (memInterval) clearInterval(memInterval);
  if (networkInterval) clearInterval(networkInterval);
});
</script>

<template>
  <component
    :is="currentShell"
    :lyric-fullscreen="lyricFullscreen"
  >
    <template #titlebar-center>
      <span v-if="!lyricFullscreen">{{ memoryUsage }}</span>
    </template>

    <template #banner>
      <div v-if="networkDegraded" class="network-banner">应用后台连接不稳定，部分功能可能暂不可用</div>
    </template>

    <template #sidebar>
      <Sidebar
        :active-view="currentView"
        @navigate="handleNavigate"
      />
    </template>

    <template #topbar>
      <Topbar
        v-model:searchQuery="searchQuery"
        @search="handleSearch"
        @toggle-tweaks="tweaksCollapsed = !tweaksCollapsed"
        @navigate="handleNavigate"
        @back="goBack"
        @forward="goForward"
      />
    </template>

    <div class="scroll" :class="{ 'page-transition-stack': isAuroraOverlap }">
      <Transition
        :mode="pageTransitionMode"
        :css="false"
        @enter="transitionEnter"
        @leave="transitionLeave"
      >
        <KeepAlive include="HomeView">
          <component
            :is="currentDescriptor.component"
            :key="currentDescriptor.cacheKey"
            v-bind="viewProps"
            @navigate="handleNavigate"
          />
        </KeepAlive>
      </Transition>
    </div>

    <template #extras>
      <Drawer
        :collapsed="tweaksCollapsed"
        @close="tweaksCollapsed = true"
      />
      <QueuePanel
        :show="isQueueOpen"
        @close="isQueueOpen = false"
      />
    </template>

    <template #playerbar>
      <PlayerBar
        :active-view="currentView"
        @navigate="handleNavigate"
        @toggle-queue="isQueueOpen = !isQueueOpen"
      />
    </template>
  </component>

  <FullscreenWindowControls v-if="lyricFullscreen" class="fs-controls-overlay" />
</template>

<style scoped>
/* Narrow, non-blocking strip — must not dominate brand/hero color hierarchy */
.network-banner {
  z-index: 7;
  background: color-mix(in srgb, var(--surface-2, #2a2520) 88%, var(--accent) 12%);
  color: var(--text-secondary, var(--ink-soft, #8a8070));
  text-align: center;
  padding: 3px 12px;
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.02em;
  font-family: var(--font-sans);
  border-bottom: 1px solid color-mix(in srgb, var(--border-subtle, #444) 70%, transparent);
}

.scroll > :deep(*) {
  min-height: 100%;
}

/* One grid cell: old and new pages overlap instead of becoming two flex rows. */
.scroll.page-transition-stack {
  display: grid;
  grid-template-columns: minmax(0, 1fr);
  align-items: start;
}
.scroll.page-transition-stack > :deep(*) {
  grid-area: 1 / 1;
  min-width: 0;
  min-height: 100%;
}

.fs-controls-overlay {
  position: fixed;
  top: 0;
  right: 0;
  z-index: 1000;
  display: flex;
  gap: 6px;
  padding: 6px 16px;
  height: 32px;
  align-items: center;
}
</style>

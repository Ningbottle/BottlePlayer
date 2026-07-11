<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';

import Sidebar from './components/Sidebar.vue';
import Topbar from './components/Topbar.vue';
import PlayerBar from './components/PlayerBar.vue';
import Drawer from './components/Drawer.vue';
import QueuePanel from './components/QueuePanel.vue';
import AuroraShell from './components/shell/AuroraShell.vue';
import NewsprintShell from './components/shell/NewsprintShell.vue';

import { initPlayer, initPlayerBackend } from './api/playerStore';
import { checkLoginStatus } from './api/userStore';
import { ping } from './api/backend';
import { invoke } from '@tauri-apps/api/core';
import { lyricFullscreen } from './api/lyricFullscreen';
import { transitionEnter, transitionLeave } from './api/motion';
import { resolveViewDescriptor, type HistoryEntry, type ViewDescriptor } from './api/viewRegistry';
import { useThemeStore } from './api/themeStore';

const themeStore = useThemeStore();
const currentShell = computed(() => themeStore.skinId.value === 'aurora' ? AuroraShell : NewsprintShell);

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
      return { isQueueOpen: isQueueOpen.value, isDrawerOpen: !tweaksCollapsed.value };
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
    <template #titlebar-center>{{ memoryUsage }}</template>

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

    <div class="scroll">
      <Transition
        mode="out-in"
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
</template>

<style scoped>
.network-banner {
  z-index: 7;
  background: var(--accent);
  color: var(--paper);
  text-align: center;
  padding: 6px 12px;
  font-size: 13px;
  font-family: var(--font-sans);
}

.scroll > :deep(*) {
  min-height: 100%;
}
</style>

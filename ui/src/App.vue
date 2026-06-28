<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';

import Sidebar from './components/Sidebar.vue';
import Topbar from './components/Topbar.vue';
import PlayerBar from './components/PlayerBar.vue';
import Drawer from './components/Drawer.vue';
import QueuePanel from './components/QueuePanel.vue';

import HomeView from './views/HomeView.vue';
import SearchView from './views/SearchView.vue';
import PlaylistView from './views/PlaylistView.vue';
import LyricView from './views/LyricView.vue';
import SettingsView from './views/SettingsView.vue';
import LoginView from './views/LoginView.vue';
import HistoryView from './views/HistoryView.vue';
import StatsView from './views/StatsView.vue';

import { initPlayer, initPlayerBackend } from './api/playerStore';
import { checkLoginStatus } from './api/userStore';
import { backendHealth } from './api/backend';
import { invoke } from '@tauri-apps/api/core';
import { getCurrentWindow } from '@tauri-apps/api/window';

const currentView = ref('home');
const searchQuery = ref('');
const playlistId = ref('');
const playlistName = ref('');
const tweaksCollapsed = ref(true);
const isQueueOpen = ref(false);
const networkDegraded = ref(false);
let networkInterval: ReturnType<typeof setInterval> | null = null;

async function updateNetworkBanner() {
  const health = await backendHealth();
  networkDegraded.value = !health.ok;
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

// Tauri App Window Controls
type AppWindow = ReturnType<typeof getCurrentWindow>;
let appWindow: AppWindow | null = null;
try {
  appWindow = getCurrentWindow();
} catch (e) {
  console.warn('Tauri app window not available', e);
}

function isTitlebarControl(target: EventTarget | null) {
  return target instanceof HTMLElement && !!target.closest('.titlebar-controls');
}

async function runWindowAction(action: () => Promise<void>, label: string) {
  try {
    await action();
  } catch (e) {
    console.warn(`Tauri window ${label} failed`, e);
  }
}



function handleTitlebarDoubleClick(event: MouseEvent) {
  if (isTitlebarControl(event.target)) return;
  toggleMaximize();
}

function minimize() {
  if (!appWindow) return;
  void runWindowAction(() => appWindow!.minimize(), 'minimize');
}

function toggleMaximize() {
  if (!appWindow) return;
  void runWindowAction(() => appWindow!.toggleMaximize(), 'toggle maximize');
}

function close() {
  if (!appWindow) return;
  void runWindowAction(() => appWindow!.close(), 'close');
}

// Navigation History Stack
interface HistoryEntry {
  view: string;
  playlistId?: string;
  playlistName?: string;
  searchQuery?: string;
}

const historyStack = ref<HistoryEntry[]>([{ view: 'home' }]);
const historyIndex = ref(0);

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
  const entry: HistoryEntry = { view };
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
  <div v-if="networkDegraded" class="network-banner">网络连接不稳定，已切换离线浏览</div>

  <!-- Newsprint procedural background layers -->
  <div class="paper-base"></div>
  <div class="paper-fibers"></div>
  <div class="paper-grain"></div>
  <div class="paper-vignette"></div>

  <!-- Main grid app shell -->
  <div class="app">
    <!-- Custom Drag-enabled Titlebar -->
    <div class="titlebar" data-tauri-drag-region @dblclick="handleTitlebarDoubleClick">
      <div class="titlebar-logo">
        <span class="logo"><i>The</i> Player</span>
      </div>
      <div class="titlebar-center">
        {{ memoryUsage }}
      </div>
      <div class="titlebar-controls" @mousedown.stop @dblclick.stop>
        <button class="control-btn min" @mousedown.stop @click.stop="minimize" title="最小化">
          <svg viewBox="0 0 10 10">
            <line x1="1" y1="5" x2="9" y2="5" stroke="currentColor" stroke-width="1.2"/>
          </svg>
        </button>
        <button class="control-btn max" @mousedown.stop @click.stop="toggleMaximize" title="最大化">
          <svg viewBox="0 0 10 10">
            <rect x="1.5" y="1.5" width="7" height="7" fill="none" stroke="currentColor" stroke-width="1.2"/>
          </svg>
        </button>
        <button class="control-btn close" @mousedown.stop @click.stop="close" title="关闭">
          <svg viewBox="0 0 10 10">
            <path d="M 2 2 L 8 8 M 8 2 L 2 8" fill="none" stroke="currentColor" stroke-width="1.2"/>
          </svg>
        </button>
      </div>
    </div>

    <!-- Sidebar Navigation -->
    <Sidebar 
      :active-view="currentView" 
      @navigate="handleNavigate" 
    />

    <!-- Main Content Area -->
    <section class="main">
      <!-- Search & actions Topbar -->
      <Topbar 
        v-model:searchQuery="searchQuery" 
        @search="handleSearch"
        @toggle-tweaks="tweaksCollapsed = !tweaksCollapsed"
        @navigate="handleNavigate"
        @back="goBack"
        @forward="goForward"
      />

      <!-- View Switcher -->
      <div class="scroll">
        <HomeView 
          v-if="currentView === 'home'" 
          @navigate="handleNavigate" 
        />
        <SearchView 
          v-else-if="currentView === 'search'" 
          :query="searchQuery" 
        />
        <PlaylistView 
          v-else-if="currentView === 'playlist'" 
          :playlist-id="playlistId"
          :playlist-name="playlistName"
        />
        <LyricView 
          v-else-if="currentView === 'lyric'" 
          :is-queue-open="isQueueOpen"
          :is-drawer-open="!tweaksCollapsed"
        />
        <SettingsView 
          v-else-if="currentView === 'settings'" 
        />
        <LoginView 
          v-else-if="currentView === 'login'" 
          @navigate="handleNavigate"
        />
        <HistoryView 
          v-else-if="currentView === 'history'" 
        />
        <StatsView
          v-else-if="currentView === 'stats'"
        />
      </div>

      <!-- Collapsible Tweaks Panel Drawer -->
      <Drawer 
        :collapsed="tweaksCollapsed" 
        @close="tweaksCollapsed = true" 
      />

      <!-- Pop-up Queue Panel (Positioned Absolute, Pointer-events Auto) -->
      <QueuePanel
        :show="isQueueOpen"
        @close="isQueueOpen = false"
      />
    </section>

    <!-- Bottom player controller bar -->
    <PlayerBar 
      :active-view="currentView"
      @navigate="handleNavigate" 
      @toggle-queue="isQueueOpen = !isQueueOpen"
    />
  </div>
</template>

<style scoped>
.network-banner {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  z-index: 9999;
  background: var(--accent);
  color: var(--paper);
  text-align: center;
  padding: 6px 12px;
  font-size: 13px;
  font-family: var(--font-sans);
}

/* App root shell layout settings */
</style>

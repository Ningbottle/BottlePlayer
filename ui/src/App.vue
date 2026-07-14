<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { RouterView, useRouter } from 'vue-router';

import Sidebar from './components/Sidebar.vue';
import Topbar from './components/Topbar.vue';
import PlayerBar from './components/PlayerBar.vue';
import Drawer from './components/Drawer.vue';
import QueuePanel from './components/QueuePanel.vue';
import AuroraShell from './components/shell/AuroraShell.vue';
import NewsprintShell from './components/shell/NewsprintShell.vue';
import FullscreenWindowControls from './components/shell/FullscreenWindowControls.vue';
import PageRecoveryBoundary from './components/shell/PageRecoveryBoundary.vue';

import { initPlayer, initPlayerBackend } from './api/playerStore';
import { checkLoginStatus } from './api/userStore';
import { ping } from './api/backend';
import { invoke } from '@tauri-apps/api/core';
import { lyricFullscreen, setLyricFullscreen } from './api/lyricFullscreen';
import { transitionEnter, transitionLeave } from './api/motion';
import { registerPageTransition, unregisterPageTransition } from './navigation/navigationLifecycle';
import { routeNames, type AppRouteName } from './navigation/routes';
import { useThemeStore } from './api/themeStore';

const themeStore = useThemeStore();
const appRouter = useRouter();
const keepAliveComponents = computed(() => appRouter.getRoutes()
  .filter((route) => route.meta.keepAlive)
  .flatMap((route) => {
    const component = route.components?.default;
    if (typeof component === 'function') return component.name ? [component.name] : [];
    if (component && typeof component === 'object' && 'name' in component) {
      return typeof component.name === 'string' ? [component.name] : [];
    }
    return [];
  }));

const currentShell = computed(() => themeStore.skinId.value === 'aurora' ? AuroraShell : NewsprintShell);
/** Aurora: simultaneous enter/leave (overlap). Newsprint: serial out-in. */
const pageTransitionMode = computed<'out-in' | undefined>(() =>
  themeStore.skinId.value === 'aurora' ? undefined : 'out-in',
);
const isAuroraOverlap = computed(() => themeStore.skinId.value === 'aurora');

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

function handleNavigate(view: string, params?: { id?: string; name?: string }) {
  if (view === routeNames.playlist && params?.id) {
    void appRouter.push({
      name: routeNames.playlist,
      params: { id: params.id },
      query: params.name ? { name: params.name } : {},
    });
    return;
  }
  void appRouter.push({ name: view as AppRouteName });
}

function handleSearch(query: string) {
  if (query.trim()) {
    void appRouter.push({ name: routeNames.search, query: { q: query } });
  }
}

function handleSearchQuery(query: string) {
  if (appRouter.currentRoute.value.name === routeNames.search) {
    void appRouter.replace({ name: routeNames.search, query: { q: query } });
  }
}

function goBack() {
  appRouter.back();
}

function goForward() {
  appRouter.forward();
}

onMounted(() => {
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
      <Sidebar @navigate="handleNavigate" />
    </template>

    <template #topbar>
      <Topbar
        @update:search-query="handleSearchQuery"
        @search="handleSearch"
        @toggle-tweaks="tweaksCollapsed = !tweaksCollapsed"
        @navigate="handleNavigate"
        @back="goBack"
        @forward="goForward"
      />
    </template>

    <div class="scroll" :class="{ 'page-transition-stack': isAuroraOverlap }">
      <PageRecoveryBoundary v-slot="{ retryKey }">
        <RouterView v-slot="{ Component, route }">
          <Transition
            :mode="pageTransitionMode"
            :css="false"
            @before-enter="registerPageTransition"
            @before-leave="registerPageTransition"
            @after-enter="unregisterPageTransition"
            @after-leave="unregisterPageTransition"
            @enter="transitionEnter"
            @leave="transitionLeave"
          >
            <KeepAlive :include="keepAliveComponents">
              <component
                :is="Component"
                :key="`${String(route.name)}:${retryKey}`"
                v-bind="route.name === routeNames.lyric ? { isQueueOpen } : {}"
                @navigate="handleNavigate"
              />
            </KeepAlive>
          </Transition>
        </RouterView>
      </PageRecoveryBoundary>
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

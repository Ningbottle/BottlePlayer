<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue';
import { RouterView, useRouter } from 'vue-router';
import { gsap } from 'gsap';

import Sidebar from './components/Sidebar.vue';
import Topbar from './components/Topbar.vue';
import PlayerBar from './playback/components/PlayerBar.vue';
import QueuePanel from './playback/components/QueuePanel.vue';
import AuroraShell from './components/shell/AuroraShell.vue';
import NewsprintShell from './components/shell/NewsprintShell.vue';
import FullscreenWindowControls from './components/shell/FullscreenWindowControls.vue';
import PageRecoveryBoundary from './components/shell/PageRecoveryBoundary.vue';

import { initPlayer, initPlayerBackend } from './playback/playerStore';
import { bindOsMediaBridge, unbindOsMediaBridge } from './playback/sync/osMediaBridge';
import { checkLoginStatus } from './api/userStore';
import { ping } from './platform/tauri/nativeClient';
import { lyricFullscreen, setLyricFullscreen } from './api/lyricFullscreen';
import { transitionEnter, transitionLeave } from './app/navigation/pageTransitions';
import { isReducedMotion } from './shared/motion/motion';
import { startPlayerSyncHost } from './playback/sync/playerSync';
import { registerPageTransition, unregisterPageTransition } from './app/navigation/navigationLifecycle';
import { routeNames, type AppRouteName } from './app/navigation/routes';
import { useThemeStore } from './app/appearance/themeStore';

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

/** Overlay windows (island / desktop lyric) render the bare view — no shell, no player init. */
const isOverlayWindow = typeof location !== 'undefined' && location.pathname.startsWith('/overlay/');
const isOverlayRoute = computed(() => isOverlayWindow || appRouter.currentRoute.value.meta.overlay === true);

const isQueueOpen = ref(false);
const networkDegraded = ref(false);
let networkInterval: ReturnType<typeof setInterval> | null = null;
let syncHostTeardown: (() => void) | null = null;

/** First-paint launch intro: shell unfolds once per app start. */
let launchPlayed = false;

function playLaunchIntro(): void {
  if (launchPlayed || isReducedMotion()) return;
  const targets = ['.titlebar', '.shell-sidebar', '.shell-content', '.shell-playerbar'];
  if (!targets.every((t) => document.querySelector(t))) return;
  launchPlayed = true;
  gsap.timeline({ defaults: { ease: 'expo.out' } })
    .from('.titlebar', { opacity: 0, duration: 0.4 }, 0)
    .from('.shell-sidebar', { x: -18, opacity: 0, duration: 0.5 }, 0.06)
    .from('.shell-content', { y: 18, opacity: 0, duration: 0.55 }, 0.14)
    .from('.shell-playerbar', { y: 22, opacity: 0, duration: 0.5 }, 0.2);
}

async function updateNetworkBanner() {
  try {
    await ping();
    networkDegraded.value = false;
  } catch (e) {
    networkDegraded.value = true;
  }
}

async function handleNavigate(view: string, params?: { id?: string; name?: string; source?: string }): Promise<boolean> {
  try {
    if (view === routeNames.playlist && params?.id) {
      await appRouter.push({
        name: routeNames.playlist,
        params: { id: params.id },
        query: {
          ...(params.name ? { name: params.name } : {}),
          ...(params.source ? { source: params.source } : {}),
        },
      });
    } else {
      await appRouter.push({ name: view as AppRouteName });
    }
    return appRouter.currentRoute.value.name === view;
  } catch {
    return false;
  }
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
  // Overlay windows run the bare view — never boot a second player instance.
  if (isOverlayRoute.value) return;

  // Don't boot into a broken fullscreen shell with zero chrome rows
  setLyricFullscreen(false);

  // Initialize HTML5 Audio element and reactive player events
  initPlayer();
  // Initialize native playback backend (falls back to HTML5)
  initPlayerBackend();
  // OS media session (T1a): only in Tauri shell (skip browser/vitest)
  const w = window as unknown as { __TAURI_INTERNALS__?: unknown; __TAURI__?: unknown };
  if (w.__TAURI_INTERNALS__ || w.__TAURI__) {
    void bindOsMediaBridge();
  }
  // Broadcast player state to overlay windows + accept their commands
  syncHostTeardown = startPlayerSyncHost();
  // Fetch initial login status
  checkLoginStatus();

  updateNetworkBanner();
  networkInterval = setInterval(updateNetworkBanner, 5_000);

  void nextTick(() => playLaunchIntro());
});

onUnmounted(() => {
  if (networkInterval) clearInterval(networkInterval);
  syncHostTeardown?.();
  void unbindOsMediaBridge();
});
</script>

<template>
  <RouterView v-if="isOverlayRoute" />
  <component
    v-else
    :is="currentShell"
    :lyric-fullscreen="lyricFullscreen"
  >

    <template #banner>
      <div v-if="networkDegraded" class="network-banner" role="status">
        网络或服务暂时不可用，本地队列与已缓存内容仍可浏览；联网后将自动恢复请求
      </div>
    </template>

    <template #sidebar>
      <Sidebar @navigate="handleNavigate" />
    </template>

    <template #topbar>
      <Topbar
        @update:search-query="handleSearchQuery"
        @search="handleSearch"
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
      <QueuePanel
        :show="isQueueOpen"
        @close="isQueueOpen = false"
      />
    </template>

    <template #playerbar>
      <PlayerBar
        :navigate="handleNavigate"
        @toggle-queue="isQueueOpen = !isQueueOpen"
      />
    </template>
  </component>

  <!-- Top-right: always show window minimize in lyric fullscreen.
       Aurora moves exit-fullscreen under the album/progress; Newsprint keeps both here. -->
  <FullscreenWindowControls
    v-if="lyricFullscreen"
    class="fs-controls-overlay"
    :show-exit="themeStore.skinId.value !== 'aurora'"
  />
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

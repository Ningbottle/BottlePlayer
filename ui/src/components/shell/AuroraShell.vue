<script setup lang="ts">
import { getCurrentWindow } from '@tauri-apps/api/window';
import WindowControls from './WindowControls.vue';

withDefaults(defineProps<{
  lyricFullscreen?: boolean;
}>(), {
  lyricFullscreen: false,
});

function handleTitlebarDoubleClick(event: MouseEvent): void {
  const target = event.target;
  if (target instanceof HTMLElement && target.closest('.titlebar-controls')) return;
  getCurrentWindow().toggleMaximize().catch(() => {});
}
</script>

<template>
  <div
    class="app aurora-app"
    data-shell="aurora"
    data-layout="immersive"
    :class="{ 'lyric-fullscreen-active': lyricFullscreen }"
  >
    <div class="titlebar" data-tauri-drag-region @dblclick="handleTitlebarDoubleClick">
      <div class="titlebar-logo" aria-label="BottleMusic">
        <span class="aurora-wordmark" data-test="shell-brand">BottleMusic</span>
      </div>
      <div class="titlebar-center">
        <slot name="titlebar-center" />
      </div>
      <div class="titlebar-controls" @mousedown.stop @dblclick.stop>
        <WindowControls />
      </div>
    </div>

    <slot name="banner" />

    <nav class="shell-sidebar" v-show="!lyricFullscreen">
      <slot name="sidebar" />
    </nav>

    <main class="shell-main">
      <div class="shell-topbar" v-show="!lyricFullscreen">
        <slot name="topbar" />
      </div>
      <div class="shell-content">
        <slot />
      </div>
      <slot name="extras" />
    </main>

    <footer class="shell-playerbar" v-show="!lyricFullscreen">
      <slot name="playerbar" />
    </footer>
  </div>
</template>

<style scoped>
</style>

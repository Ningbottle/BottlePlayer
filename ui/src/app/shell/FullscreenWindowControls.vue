<script setup lang="ts">
import { minimizeCurrentWindow } from '../../platform/tauri/windows';
import { Minimize2, Minus } from '@lucide/vue';
import { setLyricFullscreen } from '../../features/lyrics';

const props = withDefaults(
  defineProps<{
    /** Window minimize (top-right chrome). */
    showMinimize?: boolean;
    /** Exit lyric fullscreen. */
    showExit?: boolean;
  }>(),
  {
    showMinimize: true,
    showExit: true,
  },
);

async function minimize(): Promise<void> {
  try { await minimizeCurrentWindow(); } catch (e) { console.warn('Tauri window minimize failed', e); }
}

function exitFullscreen(): void {
  setLyricFullscreen(false);
}
</script>

<template>
  <div
    class="titlebar-controls fs-controls"
    data-test="fs-controls"
    data-contrast="high"
  >
    <button
      v-if="props.showMinimize"
      class="control-btn min"
      data-test="fs-minimize"
      data-tauri-drag-region="false"
      @pointerdown.stop
      @mousedown.stop
      @dblclick.stop
      @click.stop="minimize"
      title="最小化"
      aria-label="最小化"
    >
      <Minus :size="13" :stroke-width="1.7" aria-hidden="true" />
    </button>
    <button
      v-if="props.showExit"
      class="control-btn exit-fs"
      data-test="fs-exit-fullscreen"
      data-tauri-drag-region="false"
      @pointerdown.stop
      @mousedown.stop
      @dblclick.stop
      @click.stop="exitFullscreen"
      title="退出全屏"
      aria-label="退出全屏"
    >
      <Minimize2 :size="13" :stroke-width="1.7" aria-hidden="true" />
    </button>
  </div>
</template>

<style scoped>
.fs-controls {
  padding: 0;
  border: 0;
  background: transparent;
  box-shadow: none;
  backdrop-filter: none;
}

.fs-controls .control-btn {
  width: 24px;
  height: 24px;
  border-color: transparent;
  background: transparent;
  color: var(--text-primary);
  box-shadow: none;
}

.fs-controls .control-btn:hover,
.fs-controls .control-btn:focus-visible {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--accent) 12%, transparent);
  color: var(--text-primary);
}

.fs-controls .control-btn:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}
</style>

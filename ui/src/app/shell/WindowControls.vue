<script setup lang="ts">
import { minimizeCurrentWindow, toggleMaximizeCurrentWindow, closeCurrentWindow } from '../../platform/tauri/windows';

withDefaults(defineProps<{
  showMaximize?: boolean;
}>(), {
  showMaximize: true,
});

async function minimize(): Promise<void> {
  try { await minimizeCurrentWindow(); } catch (e) { console.warn('Tauri window minimize failed', e); }
}

async function toggleMaximize(): Promise<void> {
  try { await toggleMaximizeCurrentWindow(); } catch (e) { console.warn('Tauri window toggle maximize failed', e); }
}

async function close(): Promise<void> {
  try { await closeCurrentWindow(); } catch (e) { console.warn('Tauri window close failed', e); }
}
</script>

<template>
  <button
    class="control-btn min"
    @mousedown.stop
    @click.stop="minimize"
    title="最小化"
    aria-label="最小化"
  >
    <svg viewBox="0 0 10 10">
      <line x1="1" y1="5" x2="9" y2="5" stroke="currentColor" stroke-width="1.2" />
    </svg>
  </button>
  <button
    v-if="showMaximize"
    class="control-btn max"
    @mousedown.stop
    @click.stop="toggleMaximize"
    title="最大化"
    aria-label="最大化"
  >
    <svg viewBox="0 0 10 10">
      <rect x="1.5" y="1.5" width="7" height="7" fill="none" stroke="currentColor" stroke-width="1.2" />
    </svg>
  </button>
  <button
    class="control-btn close"
    @mousedown.stop
    @click.stop="close"
    title="关闭"
    aria-label="关闭"
  >
    <svg viewBox="0 0 10 10">
      <path d="M 2 2 L 8 8 M 8 2 L 2 8" fill="none" stroke="currentColor" stroke-width="1.2" />
    </svg>
  </button>
</template>

<script setup lang="ts">
import { getCurrentWindow } from '@tauri-apps/api/window';
import { setLyricFullscreen } from '../../api/lyricFullscreen';

async function minimize(): Promise<void> {
  try { await getCurrentWindow().minimize(); } catch (e) { console.warn('Tauri window minimize failed', e); }
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
    data-tauri-drag-region
  >
    <button
      class="control-btn min"
      data-test="fs-minimize"
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
      class="control-btn exit-fs"
      data-test="fs-exit-fullscreen"
      @mousedown.stop
      @click.stop="exitFullscreen"
      title="退出全屏"
      aria-label="退出全屏"
    >
      <svg viewBox="0 0 10 10">
        <path d="M 1 3.5 L 1 1 L 3.5 1 M 6.5 1 L 9 1 L 9 3.5 M 9 6.5 L 9 9 L 6.5 9 M 3.5 9 L 1 9 L 1 6.5" fill="none" stroke="currentColor" stroke-width="1.2" />
      </svg>
    </button>
  </div>
</template>

<style scoped>
.fs-controls {
  padding: 6px 12px;
  border: 1px solid color-mix(in srgb, var(--text-primary) 22%, transparent);
  border-radius: 999px;
  background: color-mix(in srgb, var(--surface-elevated) 82%, transparent);
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.28), 0 1px 0 rgba(255, 255, 255, 0.1) inset;
  backdrop-filter: blur(14px) saturate(1.25);
}

.fs-controls .control-btn {
  width: 26px;
  height: 26px;
  border-color: color-mix(in srgb, var(--text-primary) 28%, transparent);
  background: color-mix(in srgb, var(--surface-elevated) 72%, transparent);
  color: var(--text-primary);
  box-shadow: none;
}

.fs-controls .control-btn:hover,
.fs-controls .control-btn:focus-visible {
  border-color: var(--accent);
  background: color-mix(in srgb, var(--accent) 22%, var(--surface-elevated));
  color: var(--text-primary);
}

.fs-controls .control-btn:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}
</style>

<script setup lang="ts">
import { onBeforeUnmount, ref } from 'vue';
import { loadNumber, safeSetItem } from '../../platform/storage/safeStorage';

const emit = defineEmits<{ (e: 'resize', width: number): void }>();
const MIN_WIDTH = 200;
const MAX_WIDTH = 360;
const DEFAULT_WIDTH = 240;
const STORAGE_KEY = 'sidebar_width';
const width = ref(loadNumber(STORAGE_KEY, DEFAULT_WIDTH, MIN_WIDTH, MAX_WIDTH));
const dragging = ref(false);
let pointerId: number | null = null;
let startX = 0;
let startWidth = 0;
let handle: HTMLElement | null = null;

emit('resize', width.value);

function setWidth(value: number): void {
  width.value = Math.round(Math.max(MIN_WIDTH, Math.min(MAX_WIDTH, value)));
  emit('resize', width.value);
}

function finish(): void {
  if (!dragging.value) return;
  dragging.value = false;
  if (pointerId !== null && handle?.hasPointerCapture?.(pointerId)) {
    handle.releasePointerCapture(pointerId);
  }
  pointerId = null;
  handle = null;
  safeSetItem(STORAGE_KEY, String(width.value));
  window.removeEventListener('blur', finish);
}

function start(event: PointerEvent): void {
  if (event.button !== 0 || dragging.value) return;
  event.preventDefault();
  handle = event.currentTarget as HTMLElement;
  startX = event.clientX;
  startWidth = handle.parentElement?.getBoundingClientRect().width || width.value;
  pointerId = event.pointerId;
  dragging.value = true;
  handle.setPointerCapture?.(event.pointerId);
  window.addEventListener('blur', finish);
}

function move(event: PointerEvent): void {
  if (!dragging.value || event.pointerId !== pointerId) return;
  setWidth(startWidth + event.clientX - startX);
}

function reset(): void {
  setWidth(DEFAULT_WIDTH);
  safeSetItem(STORAGE_KEY, String(width.value));
}

function onKeydown(event: KeyboardEvent): void {
  const step = event.shiftKey ? 40 : 10;
  const value = event.key === 'ArrowLeft' ? width.value - step
    : event.key === 'ArrowRight' ? width.value + step
    : event.key === 'Home' ? MIN_WIDTH
    : event.key === 'End' ? MAX_WIDTH : null;
  if (value === null) return;
  event.preventDefault();
  setWidth(value);
  safeSetItem(STORAGE_KEY, String(width.value));
}

onBeforeUnmount(finish);
</script>

<template>
  <div
    class="sidebar-resize-handle"
    :class="{ 'is-dragging': dragging }"
    data-test="sidebar-resize-handle"
    role="separator"
    tabindex="0"
    aria-label="调整侧边栏宽度"
    aria-orientation="vertical"
    :aria-valuemin="MIN_WIDTH"
    :aria-valuemax="MAX_WIDTH"
    :aria-valuenow="width"
    title="拖拽调整侧边栏宽度，双击恢复默认"
    @pointerdown="start"
    @pointermove="move"
    @pointerup="finish"
    @pointercancel="finish"
    @lostpointercapture="finish"
    @dblclick="reset"
    @keydown="onKeydown"
  />
</template>

<style scoped>
.sidebar-resize-handle {
  position: absolute;
  inset: 0 0 0 auto;
  width: 7px;
  z-index: 5;
  cursor: col-resize;
  touch-action: none;
  user-select: none;
}
.sidebar-resize-handle::after {
  content: '';
  position: absolute;
  inset: 0 0 0 auto;
  width: 2px;
  background: transparent;
}
.sidebar-resize-handle:hover::after,
.sidebar-resize-handle:focus-visible::after,
.sidebar-resize-handle.is-dragging::after {
  background: var(--accent);
}
.sidebar-resize-handle:focus-visible { outline: 1px solid var(--accent); outline-offset: -1px; }
</style>

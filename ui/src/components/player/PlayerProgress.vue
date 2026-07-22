<script setup lang="ts">
import { computed, ref } from 'vue';

const props = defineProps<{
  currentTime: number;
  duration: number;
  buffered?: number;
}>();

const emit = defineEmits<{
  (e: 'seek', position: number): void;
}>();

const isEnabled = computed(() => props.duration > 0 && !isNaN(props.duration));

const progressPct = computed(() => {
  if (!isEnabled.value) return 0;
  return Math.max(0, Math.min(100, (props.currentTime / props.duration) * 100));
});

const bufferedPct = computed(() => {
  if (!isEnabled.value || props.buffered === undefined) return 0;
  return Math.max(0, Math.min(100, (props.buffered / props.duration) * 100));
});

function formatTime(sec: number) {
  if (isNaN(sec) || sec === null || sec === undefined) return '00:00';
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function handleClick(e: MouseEvent) {
  if (!isEnabled.value) return;
  const trackEl = e.currentTarget as HTMLElement;
  const rect = trackEl.getBoundingClientRect();
  const clickX = e.clientX - rect.left;
  const pct = Math.max(0, Math.min(1, clickX / rect.width));
  emit('seek', pct * props.duration);
}

function handleKeydown(e: KeyboardEvent) {
  if (!isEnabled.value) return;
  let target: number | null = null;
  switch (e.key) {
    case 'ArrowLeft':
      target = props.currentTime - 5;
      break;
    case 'ArrowRight':
      target = props.currentTime + 5;
      break;
    case 'Home':
      target = 0;
      break;
    case 'End':
      target = props.duration;
      break;
    default:
      return;
  }
  e.preventDefault();
  target = Math.max(0, Math.min(props.duration, target));
  emit('seek', target);
}

/** Hover time preview: percentage across the track, null when not hovering. */
const hoverPct = ref<number | null>(null);

const hoverTime = computed(() => {
  if (hoverPct.value === null || !isEnabled.value) return '';
  return formatTime((hoverPct.value / 100) * props.duration);
});

function handleHover(e: MouseEvent) {
  if (!isEnabled.value) {
    hoverPct.value = null;
    return;
  }
  const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
  if (rect.width <= 0) return;
  hoverPct.value = Math.max(0, Math.min(100, ((e.clientX - rect.left) / rect.width) * 100));
}

function handleHoverLeave() {
  hoverPct.value = null;
}
</script>

<template>
  <div class="progress-root">
    <span class="progress-time">{{ formatTime(currentTime) }}</span>
    <div
      class="progress-track"
      :role="isEnabled ? 'slider' : undefined"
      :tabindex="isEnabled ? 0 : -1"
      :aria-valuemin="isEnabled ? 0 : undefined"
      :aria-valuemax="isEnabled ? duration : undefined"
      :aria-valuenow="isEnabled ? currentTime : undefined"
      :aria-label="isEnabled ? '播放进度' : undefined"
      :aria-disabled="isEnabled ? undefined : 'true'"
      :style="{ '--progress-pct': progressPct + '%', '--progress-buffered-pct': bufferedPct + '%' }"
      @click="handleClick"
      @keydown="handleKeydown"
      @mousemove="handleHover"
      @mouseleave="handleHoverLeave"
    >
      <div v-if="buffered !== undefined" class="progress-buffered"></div>
      <div class="progress-fill"></div>
      <div class="progress-thumb"></div>
      <div
        v-if="hoverPct !== null && isEnabled"
        class="progress-hover-tip"
        :style="{ left: hoverPct + '%' }"
      >{{ hoverTime }}</div>
    </div>
    <span class="progress-time">{{ formatTime(duration) }}</span>
  </div>
</template>

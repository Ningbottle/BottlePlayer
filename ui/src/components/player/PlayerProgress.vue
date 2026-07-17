<script setup lang="ts">
import { computed } from 'vue';

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
    >
      <div v-if="buffered !== undefined" class="progress-buffered"></div>
      <div class="progress-fill"></div>
      <div class="progress-thumb"></div>
    </div>
    <span class="progress-time">{{ formatTime(duration) }}</span>
  </div>
</template>

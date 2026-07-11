<script setup lang="ts">
defineProps<{
  index: number;
  title: string;
  subtitle?: string;
}>();

const emit = defineEmits<{
  (e: 'click', event: MouseEvent): void;
}>();
</script>

<template>
  <div class="skin-list-row" @click="emit('click', $event)">
    <span v-if="$slots.cover" class="skin-list-row-cover">
      <slot name="cover" />
    </span>
    <span class="skin-list-row-index">{{ index }}</span>
    <div class="skin-list-row-info">
      <span class="skin-list-row-title">{{ title }}</span>
      <span v-if="subtitle" class="skin-list-row-subtitle">{{ subtitle }}</span>
    </div>
    <span v-if="$slots.meta" class="skin-list-row-meta">
      <slot name="meta" />
    </span>
  </div>
</template>

<style scoped>
.skin-list-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 4px;
  cursor: pointer;
  transition: background 0.12s ease;
}
.skin-list-row:hover {
  background: var(--surface-2);
}
.skin-list-row-cover {
  flex-shrink: 0;
}
.skin-list-row-index {
  flex-shrink: 0;
  min-width: 28px;
  text-align: center;
  color: var(--text-muted);
  font-size: 13px;
}
.skin-list-row-info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.skin-list-row-title {
  font-size: 13px;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.skin-list-row-subtitle {
  font-size: 11px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.skin-list-row-meta {
  flex-shrink: 0;
  font-size: 12px;
  color: var(--text-secondary);
}
</style>

<script setup lang="ts">
const props = withDefaults(defineProps<{
  variant?: 'primary' | 'secondary' | 'ghost';
  size?: 'sm' | 'md';
  disabled?: boolean;
}>(), {
  variant: 'secondary',
  size: 'md',
  disabled: false,
});

const emit = defineEmits<{
  (e: 'click', event: MouseEvent): void;
}>();

function onClick(event: MouseEvent) {
  if (props.disabled) return;
  emit('click', event);
}
</script>

<template>
  <button
    class="skin-button"
    :data-variant="variant"
    :data-size="size"
    :disabled="disabled"
    @click="onClick"
  >
    <slot />
  </button>
</template>

<style scoped>
.skin-button {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: background 0.15s ease, color 0.15s ease, border-color 0.15s ease, transform 0.1s ease;
  white-space: nowrap;
  font-family: var(--font-sans, system-ui, sans-serif);
}
.skin-button:active:not(:disabled) {
  transform: scale(0.97);
}
.skin-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.skin-button:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

/* Size: md (default) */
.skin-button[data-size='md'] {
  height: 36px;
  padding: 0 16px;
  font-size: 14px;
}
/* Size: sm */
.skin-button[data-size='sm'] {
  height: 30px;
  padding: 0 12px;
  font-size: 13px;
}

/* Variant base colors (overridden by skin CSS) */
.skin-button[data-variant='primary'] {
  background: var(--accent);
  color: var(--app-bg);
  border: none;
}
.skin-button[data-variant='secondary'] {
  background: var(--surface-2);
  color: var(--text-primary);
  border: 1px solid var(--border-subtle);
}
.skin-button[data-variant='ghost'] {
  background: transparent;
  color: var(--text-muted);
  border: none;
  padding: 6px 10px;
}
</style>

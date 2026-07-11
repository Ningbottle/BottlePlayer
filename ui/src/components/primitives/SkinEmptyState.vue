<script setup lang="ts">
defineProps<{
  message: string;
}>();
</script>

<template>
  <div class="skin-empty-state">
    <p class="skin-empty-state-message">{{ message }}</p>
    <div v-if="$slots.action" class="skin-empty-state-action">
      <slot name="action" />
    </div>
  </div>
</template>

<style scoped>
.skin-empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 48px 24px;
}
.skin-empty-state-message {
  margin: 0;
  font-size: 14px;
  color: var(--text-muted);
  text-align: center;
}
.skin-empty-state-action {
  display: flex;
  align-items: center;
}
</style>

<script setup lang="ts">
defineProps<{
  title: string;
  kicker?: string;
  subtitle?: string;
}>();
</script>

<template>
  <header class="skin-page-header">
    <div class="skin-page-header-main">
      <span v-if="kicker" class="skin-page-header-kicker">{{ kicker }}</span>
      <h1 class="skin-page-header-title">{{ title }}<span v-if="subtitle" class="skin-page-header-subtitle">{{ subtitle }}</span></h1>
    </div>
    <div v-if="$slots.actions" class="skin-page-header-actions">
      <slot name="actions" />
    </div>
  </header>
</template>

<style scoped>
.skin-page-header {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 24px;
  padding-bottom: 14px;
  margin-bottom: 22px;
}
.skin-page-header-main {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
}
.skin-page-header-kicker {
  font-size: 12px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
  color: var(--text-muted);
}
.skin-page-header-title {
  margin: 0;
  font-size: 32px;
  font-weight: 600;
  line-height: 1.05;
  color: var(--text-primary);
}
.skin-page-header-subtitle {
  font-size: 16px;
  font-style: italic;
  font-weight: 400;
  color: var(--text-muted);
  margin-left: 8px;
}
.skin-page-header-actions {
  flex-shrink: 0;
}
</style>

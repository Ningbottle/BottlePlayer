<script setup lang="ts">
const props = defineProps<{ autoFollowing: boolean }>();
defineEmits<{ (e: 'resume'): void }>();
</script>

<template>
  <div
    class="lyric-follow-footer"
    data-test="lyric-footer"
    :class="{ following: props.autoFollowing }"
  >
    <button
      class="return-to-current"
      data-test="return-to-current"
      @click="$emit('resume')"
    >
      回到当前行
    </button>
  </div>
</template>

<style scoped>
.lyric-follow-footer {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 44px;
  padding: 6px 16px;
  transition: visibility 0s linear 0s;
}

.lyric-follow-footer.following {
  visibility: hidden;
  pointer-events: none;
}

.return-to-current {
  padding: 7px 16px;
  border: 1px solid var(--rule-soft, var(--rule));
  border-radius: 8px;
  background: var(--paper);
  color: var(--ink);
  font-family: var(--font-sans, system-ui, sans-serif);
  font-style: normal;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.02em;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(40, 28, 12, 0.1);
  transition: transform 0.15s var(--ease-spa, ease), box-shadow 0.15s var(--ease-spa, ease), border-color 0.15s ease;
}

.return-to-current:hover {
  transform: scale(1.04);
  box-shadow: 0 4px 14px rgba(40, 28, 12, 0.18);
}
</style>

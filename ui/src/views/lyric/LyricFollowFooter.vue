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
  padding: 6px 18px;
  border: 1px solid var(--rule-soft, var(--rule));
  border-radius: 999px;
  background: var(--paper);
  color: var(--ink);
  font-family: var(--font-serif, serif);
  font-style: italic;
  font-size: 13px;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(40, 28, 12, 0.1);
  transition: transform 0.15s var(--ease-spa, ease), box-shadow 0.15s var(--ease-spa, ease);
}

.return-to-current:hover {
  transform: scale(1.04);
  box-shadow: 0 4px 14px rgba(40, 28, 12, 0.18);
}
</style>

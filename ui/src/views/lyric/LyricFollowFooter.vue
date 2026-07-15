<script setup lang="ts">
import { computed } from 'vue';
import { PhCrosshairSimple } from '@phosphor-icons/vue';
import { LocateFixed } from '@lucide/vue';
import { useThemeStore } from '../../api/themeStore';

const props = defineProps<{ autoFollowing: boolean }>();
defineEmits<{ (e: 'resume'): void }>();

const themeStore = useThemeStore();
const isAurora = computed(() => themeStore.skinId.value === 'aurora');
</script>

<template>
  <div
    class="lyric-follow-footer"
    data-test="lyric-footer"
    :class="{ following: props.autoFollowing }"
  >
    <button
      type="button"
      class="return-to-current"
      data-test="return-to-current"
      aria-label="回到当前行"
      title="回到当前行"
      @click="$emit('resume')"
    >
      <PhCrosshairSimple v-if="isAurora" :size="16" weight="bold" aria-hidden="true" />
      <LocateFixed v-else :size="16" :stroke-width="1.75" aria-hidden="true" />
    </button>
  </div>
</template>

<style scoped>
.lyric-follow-footer {
  display: flex;
  align-items: center;
  justify-content: center;
  flex: 0 0 auto;
  min-height: 36px;
  padding: 4px 12px 2px;
  transition: visibility 0s linear 0s;
}

.lyric-follow-footer.following {
  visibility: hidden;
  pointer-events: none;
}

.return-to-current {
  width: 32px;
  height: 28px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 1px solid var(--rule-soft, var(--rule));
  border-radius: 8px;
  background: var(--paper);
  color: var(--ink);
  line-height: 0;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(40, 28, 12, 0.1);
  transition: transform 0.15s var(--ease-spa, ease), box-shadow 0.15s var(--ease-spa, ease), border-color 0.15s ease;
}

.return-to-current:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 14px rgba(40, 28, 12, 0.18);
}

.return-to-current:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
}

@media (prefers-reduced-motion: reduce) {
  .return-to-current {
    transition: none;
  }

  .return-to-current:hover {
    transform: none;
  }
}
</style>

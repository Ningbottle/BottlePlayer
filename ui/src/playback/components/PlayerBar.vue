<script setup lang="ts">
import { computed } from 'vue';
import { useRoute } from 'vue-router';
import { useThemeStore } from '../../app/appearance/themeStore';
import { usePlayerControls } from './player/usePlayerControls';
import AuroraPlayerBar from './player/AuroraPlayerBar.vue';
import NewsprintPlayerBar from './player/NewsprintPlayerBar.vue';

const props = defineProps<{
  navigate?: (view: string) => void | boolean | Promise<void | boolean>;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string): void;
  (e: 'toggle-queue'): void;
}>();

const { skinId } = useThemeStore();
const route = useRoute();

const controller = usePlayerControls({
  activeView: () => route.name as string,
  onNavigate: (view: string) => {
    if (props.navigate) return props.navigate(view);
    emit('navigate', view);
  },
});

const playerBarComponent = computed(() =>
  skinId.value === 'newsprint' ? NewsprintPlayerBar : AuroraPlayerBar,
);
</script>

<template>
  <component
    :is="playerBarComponent"
    :controller="controller"
    @toggle-queue="emit('toggle-queue')"
  />
</template>

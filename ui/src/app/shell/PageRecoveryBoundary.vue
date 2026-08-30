<script setup lang="ts">
import { onErrorCaptured, ref, watch } from 'vue';
import { useRoute, useRouter } from 'vue-router';

import { cancelPageTransition } from '../navigation/navigationLifecycle';
import { clearLyricFullscreenUnlessOnLyric } from '../../api/lyricFullscreen';
import { settleActiveTransitionSessions } from '../navigation/transitionSession';
import { useThemeStore, type SkinId } from '../appearance/themeStore';

interface PageFailure {
  routeFullPath: string;
  skin: SkinId;
  error: Error;
}

const route = useRoute();
const router = useRouter();
const themeStore = useThemeStore();
const skinId = themeStore.skinId;
const retryKey = ref(0);
const failure = ref<PageFailure | null>(null);
const returningHome = ref(false);

function normalizeError(error: unknown): Error {
  return error instanceof Error ? error : new Error(String(error));
}

onErrorCaptured((error) => {
  const capturedError = normalizeError(error);
  const skin = themeStore.skinId.value;
  failure.value = {
    routeFullPath: route.fullPath,
    skin,
    error: capturedError,
  };
  console.error('[PageRecoveryBoundary]', {
    routeFullPath: route.fullPath,
    skin,
    error: capturedError,
  });
  clearLyricFullscreenUnlessOnLyric(false);
  const restoreTransitionStyles = settleActiveTransitionSessions();
  cancelPageTransition();
  restoreTransitionStyles();
  return false;
});

watch(() => route.fullPath, (_nextFullPath, previousFullPath) => {
  if (failure.value?.routeFullPath === previousFullPath) {
    failure.value = null;
  }
});

async function returnHome(): Promise<void> {
  if (returningHome.value) return;
  const failureAtStart = failure.value;
  returningHome.value = true;
  try {
    const navigationFailure = await router.replace({ name: 'home' });
    if (!navigationFailure && failure.value === failureAtStart) {
      failure.value = null;
    }
  } catch {
    // Keep the recovery state mounted so the user can retry after a failed navigation.
  } finally {
    returningHome.value = false;
  }
}

function retryCurrentPage(): void {
  failure.value = null;
  retryKey.value += 1;
}
</script>

<template>
  <div
    v-if="failure"
    class="page-recovery"
    data-testid="page-recovery"
    :data-skin="skinId"
  >
    <section class="page-recovery__panel" role="alert" aria-live="assertive">
      <p class="page-recovery__eyebrow">页面恢复</p>
      <h2 class="page-recovery__title">页面暂时无法显示</h2>
      <p class="page-recovery__message">可以返回首页，或重试当前页面。</p>
      <div class="page-recovery__actions">
        <button
          type="button"
          data-testid="page-recovery-home"
          class="page-recovery__button"
          :disabled="returningHome"
          @click="returnHome"
        >
          返回首页
        </button>
        <button
          type="button"
          data-testid="page-recovery-retry"
          class="page-recovery__button page-recovery__button--primary"
          @click="retryCurrentPage"
        >
          重试当前页面
        </button>
      </div>
    </section>
  </div>
  <slot v-else :retry-key="retryKey" />
</template>

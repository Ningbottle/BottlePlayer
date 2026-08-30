/**
 * direction.ts — 导航方向追踪（前进/后退），供页面过渡选择轴向。
 * 依据 vue-router 写入 history.state 的 position：push 递增、back 递减。
 */
import { ref } from 'vue';
import type { Router } from 'vue-router';

export const navigationDirection = ref<'forward' | 'back'>('forward');

export function initNavigationDirection(router: Router): void {
  let lastPosition = 0;
  router.afterEach(() => {
    const pos = Number(router.options.history.state.position ?? 0);
    navigationDirection.value = pos < lastPosition ? 'back' : 'forward';
    lastPosition = pos;
  });
}

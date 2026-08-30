import { gsap } from 'gsap';
import type { Router } from 'vue-router';

import { clearLyricFullscreenUnlessOnLyric } from '../../api/lyricFullscreen';
import { settleActiveTransitionSessions } from './transitionSession';
import { routeNames } from './routes';

const activePageTransitions = new Set<Element>();

function clearTransitionStyles(element: HTMLElement): void {
  element.style.opacity = '';
  element.style.transform = '';
  element.style.filter = '';
}

/** Registers a page element owned by the current RouterView transition. */
export function registerPageTransition(element: Element): void {
  activePageTransitions.add(element);
}

export function unregisterPageTransition(element: Element): void {
  activePageTransitions.delete(element);
}

export function cancelPageTransition(): void {
  const restoreTransitionStyles = settleActiveTransitionSessions();
  const elements = [...activePageTransitions];
  for (const element of elements) {
    gsap.killTweensOf(element);
  }
  for (const element of elements) {
    clearTransitionStyles(element as HTMLElement);
    activePageTransitions.delete(element);
  }
  restoreTransitionStyles();
}

export function installNavigationLifecycle(router: Router): void {
  router.beforeEach((to) => {
    clearLyricFullscreenUnlessOnLyric(to.name === routeNames.lyric);
    cancelPageTransition();
  });
}

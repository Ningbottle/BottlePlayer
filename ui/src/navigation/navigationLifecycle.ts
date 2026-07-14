import { gsap } from 'gsap';
import type { Router } from 'vue-router';

import { setLyricFullscreen } from '../api/lyricFullscreen';
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
  const elements = [...activePageTransitions];
  for (const element of elements) {
    gsap.killTweensOf(element);
  }
  for (const element of elements) {
    clearTransitionStyles(element as HTMLElement);
    activePageTransitions.delete(element);
  }
}

export function installNavigationLifecycle(router: Router): void {
  router.beforeEach((to, from) => {
    if (from.name === routeNames.lyric && to.name !== routeNames.lyric) {
      setLyricFullscreen(false);
    }
    cancelPageTransition();
  });
}

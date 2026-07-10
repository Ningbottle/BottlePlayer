import { gsap } from 'gsap';
import type { Ref } from 'vue';

export interface CountUpOptions {
  duration?: number;
  ease?: string;
  delay?: number;
  isActive?: () => boolean;
}

/** Animate a ref from its current value to target, rounding on each update. */
export function animateCountUp(ref: Ref<number>, target: number, opts: CountUpOptions = {}): Promise<void> {
  if (isReducedMotion()) {
    if (opts.isActive?.() ?? true) ref.value = target;
    return Promise.resolve();
  }
  const obj = { value: ref.value };
  return new Promise((resolve) => {
    gsap.to(obj, {
      value: target,
      duration: opts.duration ?? 0.9,
      ease: opts.ease ?? 'expo.out',
      delay: opts.delay ?? 0,
      onUpdate: () => {
        if (opts.isActive?.() ?? true) ref.value = Math.round(obj.value);
      },
      onComplete: () => {
        if (opts.isActive?.() ?? true) ref.value = target;
        resolve();
      },
    });
  });
}

/** Animate a bar element's height to targetPx. */
export function animateBarHeight(el: HTMLElement, targetPx: number, opts: { duration?: number; ease?: string; delay?: number } = {}): void {
  gsap.killTweensOf(el);
  if (isReducedMotion()) {
    el.style.height = `${targetPx}px`;
    return;
  }
  gsap.to(el, {
    height: targetPx,
    duration: opts.duration ?? 0.55,
    ease: opts.ease ?? 'expo.out',
    delay: opts.delay ?? 0,
  });
}

/** Crossfade the app: dip opacity, swap theme at the bottom, restore opacity. */
export function crossfadeTheme(applyFn: () => void): Promise<void> {
  const app = document.querySelector('.app') as HTMLElement | null;
  if (!app || isReducedMotion()) {
    applyFn();
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    const tl = gsap.timeline({ onComplete: resolve });
    tl.to(app, { opacity: 0.3, duration: 0.15, ease: 'power2.out' });
    tl.add(() => applyFn());
    tl.to(app, { opacity: 1, duration: 0.15, ease: 'power2.out' });
  });
}

/** Vue <Transition> JS hook: enter (fade + translateY). */
export function transitionEnter(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  gsap.fromTo(el, { opacity: 0, y: 20 }, {
    opacity: 1, y: 0, duration: 0.34, ease: 'expo.out', onComplete: done,
  });
}

/** Vue <Transition> JS hook: leave (fade + translateY). */
export function transitionLeave(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  gsap.to(el, { opacity: 0, y: -16, duration: 0.18, ease: 'power2.in', onComplete: done });
}

/** True when the OS prefers reduced motion. */
export function isReducedMotion(): boolean {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches === true;
}

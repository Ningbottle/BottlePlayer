import { gsap } from 'gsap';
import type { Ref } from 'vue';

export interface CountUpOptions {
  duration?: number;
  ease?: string;
  delay?: number;
}

/** Animate a ref from its current value to target, rounding on each update. */
export function animateCountUp(ref: Ref<number>, target: number, opts: CountUpOptions = {}): Promise<void> {
  const obj = { value: ref.value };
  return new Promise((resolve) => {
    gsap.to(obj, {
      value: target,
      duration: opts.duration ?? 0.8,
      ease: opts.ease ?? 'power2.out',
      delay: opts.delay ?? 0,
      onUpdate: () => { ref.value = Math.round(obj.value); },
      onComplete: () => { ref.value = target; resolve(); },
    });
  });
}

/** Animate a bar element's height to targetPx. */
export function animateBarHeight(el: HTMLElement, targetPx: number, opts: { duration?: number; ease?: string } = {}): void {
  gsap.to(el, {
    height: targetPx,
    duration: opts.duration ?? 0.4,
    ease: opts.ease ?? 'power2.out',
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
  gsap.fromTo(el, { opacity: 0, y: 12 }, {
    opacity: 1, y: 0, duration: 0.25, ease: 'power2.out', onComplete: done,
  });
}

/** Vue <Transition> JS hook: leave (fade + translateY). */
export function transitionLeave(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  gsap.to(el, { opacity: 0, y: -12, duration: 0.2, ease: 'power2.in', onComplete: done });
}

/** True when the OS prefers reduced motion. */
export function isReducedMotion(): boolean {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches === true;
}

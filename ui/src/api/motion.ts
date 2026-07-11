import { gsap } from 'gsap';
import type { Ref } from 'vue';
import { useThemeStore } from './themeStore';
import { getMotionProfile } from './motionProfiles';
import type { ProfileKey, TweenSpec } from './motionProfiles';

export interface CountUpOptions {
  duration?: number;
  ease?: string;
  delay?: number;
  isActive?: () => boolean;
}

export interface MotionHandle {
  kill(): void;
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

function currentProfile() {
  return getMotionProfile(useThemeStore().skinId.value);
}

/** Vue <Transition> JS hook: enter (fade + translateY). */
export function transitionEnter(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  const spec = currentProfile().pageEnter;
  gsap.fromTo(el, { opacity: 0, y: 20 }, {
    opacity: 1, y: 0, duration: spec.duration, ease: spec.ease, delay: spec.delay ?? 0, onComplete: done,
  });
}

/** Vue <Transition> JS hook: leave (fade + translateY). */
export function transitionLeave(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  const spec = currentProfile().pageLeave;
  gsap.to(el, { opacity: 0, y: -16, duration: spec.duration, ease: spec.ease, delay: spec.delay ?? 0, onComplete: done });
}

/** Animate a single element using a profile key. Returns a cancellable handle. */
export function animateElement(
  el: Element,
  from: Record<string, unknown>,
  to: Record<string, unknown>,
  profileKey: ProfileKey,
): MotionHandle {
  gsap.killTweensOf(el);
  const spec = currentProfile()[profileKey] as TweenSpec;

  if (isReducedMotion()) {
    gsap.set(el, to);
    return { kill: () => {} };
  }

  const tween = gsap.fromTo(el, from, {
    ...to,
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
  });
  return { kill: () => { tween.kill(); gsap.killTweensOf(el); } };
}

/** Animate a list of elements with stagger using the cardEnter profile. Returns a cancellable handle. */
export function animateStagger(
  elements: Element[],
  profileKey: 'cardEnter',
): MotionHandle {
  const spec = currentProfile()[profileKey];
  const capped = elements.slice(0, spec.maxItems);

  capped.forEach((el) => gsap.killTweensOf(el));

  if (isReducedMotion()) {
    gsap.set(capped, { opacity: 1, y: 0 });
    return { kill: () => {} };
  }

  const tween = gsap.fromTo(capped, { opacity: 0, y: 20 }, {
    opacity: 1, y: 0,
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    stagger: spec.stagger,
  });
  return { kill: () => { tween.kill(); capped.forEach((el) => gsap.killTweensOf(el)); } };
}

/** Start ambient breathing motion for the stage element. Aurora-only, respects visibility and playback. */
export function startAmbientMotion(
  el: HTMLElement,
  isPlayingRef: Ref<boolean> | (() => boolean),
): MotionHandle {
  const profile = currentProfile();

  if (!profile.ambient.enabled || isReducedMotion()) {
    return { kill: () => {} };
  }

  const isPlaying = typeof isPlayingRef === 'function' ? isPlayingRef : () => isPlayingRef.value;
  let tween: { kill: () => void } | null = null;
  let killed = false;

  function start(): void {
    if (killed || !isPlaying() || document.hidden) return;
    gsap.killTweensOf(el);
    tween = gsap.to(el, {
      scale: profile.ambient.scale,
      duration: profile.ambient.duration,
      ease: 'sine.inOut',
      yoyo: true,
      repeat: -1,
    });
  }

  function pause(): void {
    if (tween) { tween.kill(); tween = null; }
    gsap.killTweensOf(el);
  }

  function onVisibilityChange(): void {
    if (document.hidden) {
      pause();
    } else {
      start();
    }
  }

  function onBlur(): void { pause(); }
  function onFocus(): void { start(); }

  document.addEventListener('visibilitychange', onVisibilityChange);
  window.addEventListener('blur', onBlur);
  window.addEventListener('focus', onFocus);

  start();

  return {
    kill(): void {
      killed = true;
      pause();
      document.removeEventListener('visibilitychange', onVisibilityChange);
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('focus', onFocus);
    },
  };
}

/** True when the OS prefers reduced motion. */
export function isReducedMotion(): boolean {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches === true;
}

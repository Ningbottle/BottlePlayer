import { gsap } from 'gsap';
import type { Ref } from 'vue';
import { useThemeStore } from './themeStore';
import { getMotionProfile } from './motionProfiles';
import type { ProfileKey, TweenSpec } from './motionProfiles';
import { beginTransitionSession } from './transitionSession';
import { navigationDirection } from '../navigation/direction';

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
    tl.to(app, { opacity: 0.25, scale: 0.985, duration: 0.2, ease: 'power2.in' });
    tl.add(() => applyFn());
    tl.to(app, { opacity: 1, scale: 1, duration: 0.38, ease: 'expo.out' });
  });
}

function currentProfile() {
  return getMotionProfile(useThemeStore().skinId.value);
}

/** Vue <Transition> JS hook: enter. Aurora: directional shared-axis X; Newsprint: translateY. Kill-safe via transitionSession. */
export function transitionEnter(el: Element, done?: () => void): void {
  const session = beginTransitionSession(el, 'enter', done);
  gsap.killTweensOf(el);
  if (isReducedMotion()) {
    gsap.set(el, { opacity: 1, x: 0, y: 0, clearProps: 'transform,opacity' });
    session.complete();
    return;
  }
  const spec = currentProfile().pageEnter;
  const isAurora = useThemeStore().skinId.value === 'aurora';
  const dir = navigationDirection.value === 'back' ? -1 : 1;
  const fromVars = isAurora ? { opacity: 0, x: 24 * dir } : { opacity: 0, y: spec.fromY ?? 16 };
  const toVars = isAurora ? { opacity: 1, x: 0 } : { opacity: 1, y: 0 };
  gsap.fromTo(el, fromVars, {
    ...toVars,
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    onComplete: () => {
      gsap.set(el, { clearProps: 'transform,opacity' });
      session.complete();
    },
    onInterrupt: () => {
      gsap.set(el, { clearProps: 'transform,opacity' });
      session.interrupt();
    },
  });
}

/** Vue <Transition> JS hook: leave. Kill-safe via transitionSession. */
export function transitionLeave(el: Element, done?: () => void): void {
  const session = beginTransitionSession(el, 'leave', done);
  (el as HTMLElement).style.pointerEvents = 'none';
  gsap.killTweensOf(el);
  if (isReducedMotion()) {
    gsap.set(el, { opacity: 0, x: 0, y: 0, clearProps: 'transform,opacity' });
    session.complete();
    return;
  }
  const spec = currentProfile().pageLeave;
  const isAurora = useThemeStore().skinId.value === 'aurora';
  const dir = navigationDirection.value === 'back' ? -1 : 1;
  gsap.to(el, {
    opacity: 0,
    ...(isAurora ? { x: -16 * dir } : { y: -16 }),
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    onComplete: () => {
      gsap.set(el, { clearProps: 'transform,opacity' });
      session.complete();
    },
    onInterrupt: () => {
      gsap.set(el, { clearProps: 'transform,opacity' });
      session.interrupt();
    },
  });
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

/** Optional overrides for home cold/return (and other) stagger budgets. */
export interface StaggerOverrides {
  duration?: number;
  stagger?: number;
  maxItems?: number;
  fromY?: number;
}

/**
 * Q-bounce press: squash on mousedown, elastic spring on mouseup/leave.
 * Aurora transport / chrome only — respects reduced motion.
 */
export function pressBounceDown(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  gsap.killTweensOf(el);
  if (isReducedMotion()) {
    el.style.transform = 'scale(0.94)';
    return;
  }
  const spec = currentProfile().controlPress;
  gsap.to(el, {
    scale: 0.86,
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    overwrite: true,
  });
}

export function pressBounceUp(el: Element): void {
  if (!(el instanceof HTMLElement)) return;
  gsap.killTweensOf(el);
  if (isReducedMotion()) {
    el.style.transform = '';
    return;
  }
  const spec = currentProfile().controlRelease;
  gsap.to(el, {
    scale: 1,
    duration: spec.duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    overwrite: true,
    onComplete: () => {
      gsap.set(el, { clearProps: 'transform' });
    },
  });
}

/** Animate a list of elements with stagger using the cardEnter profile. Returns a cancellable handle. */
export function animateStagger(
  elements: Element[],
  profileKey: 'cardEnter',
  overrides?: StaggerOverrides,
): MotionHandle {
  const spec = currentProfile()[profileKey];
  const maxItems = overrides?.maxItems ?? spec.maxItems;
  const duration = overrides?.duration ?? spec.duration;
  const stagger = overrides?.stagger ?? spec.stagger;
  const fromY = overrides?.fromY ?? 20;
  const capped = elements.slice(0, maxItems);
  if (capped.length === 0) return { kill: () => {} };

  capped.forEach((el) => gsap.killTweensOf(el));

  if (isReducedMotion()) {
    gsap.set(capped, { opacity: 1, y: 0 });
    return { kill: () => {} };
  }

  const tween = gsap.fromTo(capped, { opacity: 0, y: fromY }, {
    opacity: 1, y: 0,
    duration,
    ease: spec.ease,
    delay: spec.delay ?? 0,
    stagger,
  });
  return { kill: () => { tween.kill(); capped.forEach((el) => gsap.killTweensOf(el)); } };
}

/**
 * Magnetic hover: the element drifts toward the cursor by a small capped
 * offset and springs back with the skin's controlRelease profile on leave.
 * Returns a detach function. Reduced motion → inert.
 */
export function attachMagnet(el: HTMLElement, strength = 0.18, maxOffset = 3): () => void {
  if (isReducedMotion()) return () => {};

  function onMove(e: MouseEvent): void {
    const r = el.getBoundingClientRect();
    const dx = e.clientX - (r.left + r.width / 2);
    const dy = e.clientY - (r.top + r.height / 2);
    gsap.to(el, {
      x: Math.max(-maxOffset, Math.min(maxOffset, dx * strength)),
      y: Math.max(-maxOffset, Math.min(maxOffset, dy * strength)),
      duration: 0.25,
      ease: 'power2.out',
    });
  }

  function onLeave(): void {
    const spec = currentProfile().controlRelease;
    gsap.to(el, { x: 0, y: 0, duration: spec.duration, ease: spec.ease });
  }

  el.addEventListener('mousemove', onMove);
  el.addEventListener('mouseleave', onLeave);
  return () => {
    el.removeEventListener('mousemove', onMove);
    el.removeEventListener('mouseleave', onLeave);
  };
}

export interface VinylSpinHandle {
  kill: () => void;
  /** Re-read isPlayingRef and ramp the deck toward the matching state. */
  setPlaying: () => void;
  /** Scratch burst: brief spin-up to 3× and ease back to 1× (seek feedback). */
  burst: () => void;
}

/**
 * Turntable spin for the Aurora hero vinyl. Infinite GSAP rotation whose
 * timeScale ramps 0↔1 over profile.vinyl.rampSeconds, so the record speeds
 * up / winds down like a real deck. Honors visibility, blur/focus,
 * reduced-motion, and the skin profile (Newsprint → inert).
 */
export function startVinylSpin(
  el: HTMLElement,
  isPlayingRef: Ref<boolean> | (() => boolean),
): VinylSpinHandle {
  const profile = currentProfile();
  const inert: VinylSpinHandle = { kill: () => {}, setPlaying: () => {}, burst: () => {} };
  if (!profile.vinyl.enabled || isReducedMotion()) return inert;

  const isPlaying = typeof isPlayingRef === 'function' ? isPlayingRef : () => isPlayingRef.value;
  const spin = gsap.to(el, {
    rotation: '+=360',
    duration: profile.vinyl.spinSeconds,
    ease: 'none',
    repeat: -1,
    paused: true,
  });
  let killed = false;
  let ramp: { kill: () => void } | null = null;

  function rampTo(target: 0 | 1): void {
    if (ramp) { ramp.kill(); ramp = null; }
    if (target === 1) spin.play();
    ramp = gsap.to(spin, {
      timeScale: target,
      duration: profile.vinyl.rampSeconds,
      ease: target === 1 ? 'power2.out' : 'power2.inOut',
      onComplete: () => { if (target === 0) spin.pause(); },
    });
  }

  function sync(): void {
    if (killed) return;
    rampTo(isPlaying() && !document.hidden ? 1 : 0);
  }

  function burst(): void {
    if (killed || !isPlaying() || document.hidden) return;
    if (ramp) { ramp.kill(); ramp = null; }
    spin.play();
    ramp = gsap.to(spin, {
      timeScale: 3,
      duration: 0.18,
      ease: 'power2.in',
      onComplete: () => {
        if (killed) return;
        if (ramp) { ramp.kill(); ramp = null; }
        ramp = gsap.to(spin, { timeScale: 1, duration: 0.55, ease: 'power2.out' });
      },
    });
  }

  function onVisibilityChange(): void { sync(); }
  function onBlur(): void { if (!killed) rampTo(0); }
  function onFocus(): void { sync(); }

  document.addEventListener('visibilitychange', onVisibilityChange);
  window.addEventListener('blur', onBlur);
  window.addEventListener('focus', onFocus);
  sync();

  return {
    kill(): void {
      killed = true;
      if (ramp) { ramp.kill(); ramp = null; }
      spin.kill();
      document.removeEventListener('visibilitychange', onVisibilityChange);
      window.removeEventListener('blur', onBlur);
      window.removeEventListener('focus', onFocus);
    },
    setPlaying(): void { sync(); },
    burst,
  };
}

/** True when the OS prefers reduced motion. */
export function isReducedMotion(): boolean {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches === true;
}

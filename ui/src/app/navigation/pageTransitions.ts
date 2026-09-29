import { gsap } from 'gsap';
import { useThemeStore } from '../appearance/themeStore';
import { getMotionProfile } from '../../shared/motion/motionProfiles';
import { beginTransitionSession } from './transitionSession';
import { navigationDirection } from './direction';
import { isReducedMotion } from '../../shared/motion/motion';

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
  const fromVars = isAurora ? { opacity: 0, x: (spec.fromY ?? 12) * dir } : { opacity: 0, y: spec.fromY ?? 16 };
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
    // out-in installs its placeholder after this hook returns. Completing
    // synchronously re-enters Vue's patch before that subtree exists (notably
    // with KeepAlive), leaving navigation blank. No visual delay is needed;
    // let the current patch finish before removing the old page.
    queueMicrotask(() => session.complete());
    return;
  }
  const spec = currentProfile().pageLeave;
  const isAurora = useThemeStore().skinId.value === 'aurora';
  const dir = navigationDirection.value === 'back' ? -1 : 1;
  gsap.to(el, {
    opacity: 0,
    ...(isAurora ? { x: -8 * dir } : { y: -16 }),
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

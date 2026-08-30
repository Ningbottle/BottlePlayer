/**
 * Transitional motion facade: pure GSAP primitives live in motionPrimitives;
 * the route-transition adapter lives in pageTransitions. Explicit re-exports
 * only. C5b moves both to their final directories.
 */
export {
  animateCountUp,
  animateBarHeight,
  crossfadeTheme,
  animateElement,
  pressBounceDown,
  pressBounceUp,
  animateStagger,
  attachMagnet,
  startVinylSpin,
  isReducedMotion,
} from './motionPrimitives';
export type { CountUpOptions, MotionHandle, StaggerOverrides, VinylSpinHandle } from './motionPrimitives';
export { transitionEnter, transitionLeave } from './pageTransitions';

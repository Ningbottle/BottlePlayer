/**
 * coverFlight.ts — GSAP Flip shared-element cover flight.
 *
 * When the user starts a track from the home stage, a ghost of the clicked
 * cover flies from its source rect into the dock cover slot. The real cover
 * images never move; the ghost is removed on landing. Reduced motion skips.
 */
import { gsap } from 'gsap';
import { Flip } from 'gsap/Flip';
import { isReducedMotion } from './motion';

gsap.registerPlugin(Flip);

const GHOST_CLASS = 'aurora-cover-ghost';
const DOCK_COVER_SELECTOR = '.aurora-pb-cover';

export function flyCoverToDock(fromEl: HTMLElement, imgUrl: string): void {
  if (isReducedMotion() || !imgUrl) return;
  const target = document.querySelector<HTMLElement>(DOCK_COVER_SELECTOR);
  if (!target) return;

  const ghost = document.createElement('img');
  ghost.src = imgUrl;
  ghost.className = GHOST_CLASS;
  ghost.alt = '';
  ghost.setAttribute('aria-hidden', 'true');
  Object.assign(ghost.style, {
    position: 'fixed',
    margin: '0',
    pointerEvents: 'none',
    zIndex: '9999',
    borderRadius: '10px',
    objectFit: 'cover',
  } satisfies Partial<CSSStyleDeclaration>);

  const from = fromEl.getBoundingClientRect();
  Object.assign(ghost.style, {
    left: `${from.left}px`,
    top: `${from.top}px`,
    width: `${from.width}px`,
    height: `${from.height}px`,
  });
  document.body.appendChild(ghost);

  Flip.fit(ghost, target, {
    duration: 0.55,
    ease: 'expo.inOut',
    absolute: true,
    opacity: 0.9,
    onComplete: () => ghost.remove(),
  });
}

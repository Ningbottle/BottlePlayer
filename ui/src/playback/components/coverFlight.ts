/**
 * coverFlight.ts — GSAP Flip shared-element cover flight.
 *
 * When the user starts a track from the home stage, a ghost of the clicked
 * cover flies from its source rect into the dock cover slot. The real cover
 * images never move; the ghost is removed on landing. Reduced motion skips.
 */
import { gsap } from 'gsap';
import { Flip } from 'gsap/Flip';
import { isReducedMotion } from '../../shared/motion/motion';

let flipRegistered = false;
/** Register the Flip plugin on first real use (keeps module-eval mock-safe). */
function ensureFlipRegistered(): void {
  if (flipRegistered) return;
  gsap.registerPlugin(Flip);
  flipRegistered = true;
}

const GHOST_CLASS = 'aurora-cover-ghost';
const DOCK_COVER_SELECTOR = '.aurora-pb-cover';

/**
 * Shared-element cover flight between any two elements. The ghost is cloned
 * from the source rect and Flip.fit-animated onto the target rect; both real
 * covers stay put. delayMs lets a route transition mount the target first.
 */
export function flyCoverToElement(
  fromEl: HTMLElement,
  targetSelector: string,
  imgUrl: string,
  delayMs = 0,
): void {
  if (isReducedMotion() || !imgUrl) return;

  const run = (): void => {
    const target = document.querySelector<HTMLElement>(targetSelector);
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

    ensureFlipRegistered();
    Flip.fit(ghost, target, {
      duration: 0.55,
      ease: 'expo.inOut',
      absolute: true,
      opacity: 0.9,
      borderRadius: '50%',
      onComplete: () => ghost.remove(),
    });
  };

  if (delayMs > 0) {
    setTimeout(run, delayMs);
  } else {
    run();
  }
}

export function flyCoverToDock(fromEl: HTMLElement, imgUrl: string): void {
  flyCoverToElement(fromEl, DOCK_COVER_SELECTOR, imgUrl);
}

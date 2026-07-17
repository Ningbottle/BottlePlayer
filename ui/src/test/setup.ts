import { vi } from 'vitest';

// ---------------------------------------------------------------------------
// Unified HTMLMediaElement + Canvas mocks
// Purpose: silence jsdom "Not implemented" noise for media/canvas so real
// test failures are visible. These mocks are inert by default; tests that
// need real behavior override them locally.
// ---------------------------------------------------------------------------

// HTMLMediaElement mocks: patch the prototype directly so that elements
// created via document.createElement('audio') / <video> inherit the stubs.
// Use plain no-op functions (not vi.fn()) so existing tests can spyOn them
// without interference from a global mock.
// Guard: only install if one of the prototype methods is not yet implemented.
const mediaProto =
  typeof HTMLMediaElement !== 'undefined' && HTMLMediaElement.prototype;
const noop = () => undefined;
const makeNoop = () => Object.assign(
  () => undefined,
  { _isMocked: true as const },
);
if (
  mediaProto &&
  (!mediaProto.play ||
    (mediaProto.play as any)._isMocked !== true)
) {
  mediaProto.play = Object.assign(
    () => Promise.resolve(),
    { _isMocked: true as const },
  );
  mediaProto.pause = makeNoop();
  mediaProto.load = makeNoop();
  mediaProto.canPlayType = Object.assign(
    () => '',
    { _isMocked: true as const },
  );
}

// ---------------------------------------------------------------------------
// Inert CanvasRenderingContext2D mock via Proxy
// Returns a no-op function for any property access, so existing code that
// calls ctx.method() never throws. Tests that need real canvas behavior
// override getContext locally.
// ---------------------------------------------------------------------------

function createInertCanvasGradient(): any {
  return { addColorStop: noop };
}

function createInertCanvasContext(contextType: string): any {
  if (contextType === 'webgl' || contextType === 'experimental-webgl') {
    return null; // webgl is not used in these tests; return null to avoid false positives
  }
  // 2d context: use a Proxy that returns no-op functions for any property
  return new Proxy(
    {
      canvas: { width: 0, height: 0 },
      fillStyle: '',
      strokeStyle: '',
      lineWidth: 1,
      globalAlpha: 1,
      globalCompositeOperation: 'source-over',
      font: '',
      textAlign: 'start',
      textBaseline: 'alphabetic',
      shadowBlur: 0,
      shadowColor: '',
      shadowOffsetX: 0,
      shadowOffsetY: 0,
      // Methods that return objects with their own methods
      createRadialGradient: createInertCanvasGradient,
      createLinearGradient: createInertCanvasGradient,
    },
    {
      get(target, prop) {
        if (prop in target) {
          return (target as any)[prop];
        }
        // Return a no-op function for any method call
        return noop;
      },
      set(target, prop, value) {
        (target as any)[prop] = value;
        return true;
      },
    },
  );
}

HTMLCanvasElement.prototype.getContext = vi.fn().mockImplementation(
  (contextType: string) => createInertCanvasContext(contextType),
) as any;
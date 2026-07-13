export type HomeEnterMode = 'cold' | 'return';

let homeActivationCount = 0;

/** First Aurora home activation in the app session is cold; later ones are return. */
export function nextHomeEnterMode(): HomeEnterMode {
  const mode = homeActivationCount === 0 ? 'cold' : 'return';
  homeActivationCount += 1;
  return mode;
}

/** Test-only: reset activation counter so the next mode is cold again. */
export function __resetHomeEnterSessionForTest(): void {
  homeActivationCount = 0;
}

/**
 * Account cross-module effects port.
 *
 * The account store must not know about Library (favoriteStore) or Playback
 * (recentPlayedStore). The composition root configures the real side effects
 * here; account code only emits notifications. Unconfigured = safe no-op, so
 * module import order never changes behavior.
 */

export interface AccountEffects {
  onAccountReady(userId: string): void | Promise<void>;
  onAccountCleared(): void;
  onLocalLogout(): void;
}

let effects: AccountEffects | null = null;

const noopEffects: AccountEffects = {
  onAccountReady: () => undefined,
  onAccountCleared: () => undefined,
  onLocalLogout: () => undefined,
};

export function configureAccountEffects(configured: AccountEffects): void {
  effects = configured;
}

/** Test-only: restore the default no-op effects between tests. */
export function __resetAccountEffectsForTests(): void {
  effects = null;
}

function active(): AccountEffects {
  return effects ?? noopEffects;
}

/** Fire-and-forget friendly: returns the callee's Promise unchanged when given. */
export function notifyAccountReady(userId: string): void | Promise<void> {
  return active().onAccountReady(userId);
}

export function notifyAccountCleared(): void {
  active().onAccountCleared();
}

export function notifyLocalLogout(): void {
  active().onLocalLogout();
}

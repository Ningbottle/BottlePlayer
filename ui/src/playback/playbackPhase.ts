/**
 * Explicit playback phase state machine (Phase 1 stability).
 *
 * Pure helpers only — no I/O, no Vue, no backend.
 *
 * Legal edges (plan minimum + restore/quality shortcuts):
 *   idle       → resolving | idle | playing | recovering | error | loading | paused
 *   resolving  → loading | error | idle | playing
 *   loading    → playing | error | idle | resolving | paused
 *   playing    → paused | loading | resolving | error | idle | recovering
 *   paused     → playing | resolving | loading | idle | error | recovering
 *   recovering → playing | loading | resolving | error | idle
 *   error      → idle | resolving | loading | playing | recovering
 */

export type PlaybackPhase =
  | 'idle'
  | 'resolving'
  | 'loading'
  | 'playing'
  | 'paused'
  | 'recovering'
  | 'error';

const LEGAL: Record<PlaybackPhase, ReadonlySet<PlaybackPhase>> = {
  // idle also accepts restore/resume/fail shortcuts (session restore, tests, quality)
  idle: new Set(['resolving', 'idle', 'playing', 'recovering', 'error', 'loading', 'paused']),
  resolving: new Set(['loading', 'error', 'idle', 'playing']),
  loading: new Set(['playing', 'error', 'idle', 'resolving', 'paused']),
  playing: new Set(['paused', 'loading', 'resolving', 'error', 'idle', 'recovering']),
  paused: new Set(['playing', 'resolving', 'loading', 'idle', 'error', 'recovering']),
  recovering: new Set(['playing', 'loading', 'resolving', 'error', 'idle']),
  error: new Set(['idle', 'resolving', 'loading', 'playing', 'recovering']),
};

/** True when `from → to` is a documented legal edge. */
export function canTransition(from: PlaybackPhase, to: PlaybackPhase): boolean {
  return LEGAL[from]?.has(to) ?? false;
}

/**
 * Return `to` when the transition is legal; otherwise throw.
 * Error message always contains `illegal_playback_transition`.
 */
export function transitionPhase(from: PlaybackPhase, to: PlaybackPhase): PlaybackPhase {
  if (!canTransition(from, to)) {
    throw new Error(`illegal_playback_transition: ${from} → ${to}`);
  }
  return to;
}

/**
 * Project compatibility flags from authoritative phase (one-way only).
 * - playing  → isPlaying=true
 * - all other phases → isPlaying=false
 * - resolving | loading | recovering → isLoading=true
 */
export function flagsFromPhase(phase: PlaybackPhase): {
  isPlaying: boolean;
  isLoading: boolean;
} {
  return {
    isPlaying: phase === 'playing',
    isLoading:
      phase === 'resolving' || phase === 'loading' || phase === 'recovering',
  };
}

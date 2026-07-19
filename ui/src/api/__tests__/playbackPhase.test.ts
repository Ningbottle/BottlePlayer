import { describe, it, expect } from 'vitest';
import {
  canTransition,
  flagsFromPhase,
  transitionPhase,
  type PlaybackPhase,
} from '../playbackPhase';

/**
 * Legal edges: plan minimum plus restore/quality shortcuts
 * (idle→playing/recovering/error/loading/paused, resolving→playing, etc.).
 */
const LEGAL: Record<PlaybackPhase, readonly PlaybackPhase[]> = {
  idle: ['resolving', 'idle', 'playing', 'recovering', 'error', 'loading', 'paused'],
  resolving: ['loading', 'error', 'idle', 'playing'],
  loading: ['playing', 'error', 'idle', 'resolving', 'paused'],
  playing: ['paused', 'loading', 'resolving', 'error', 'idle', 'recovering'],
  paused: ['playing', 'resolving', 'loading', 'idle', 'error', 'recovering'],
  recovering: ['playing', 'loading', 'resolving', 'error', 'idle'],
  error: ['idle', 'resolving', 'loading', 'playing', 'recovering'],
};

const ALL_PHASES: PlaybackPhase[] = [
  'idle',
  'resolving',
  'loading',
  'playing',
  'paused',
  'recovering',
  'error',
];

describe('playbackPhase state machine', () => {
  it('exposes the full phase union via legal table keys', () => {
    expect(Object.keys(LEGAL).sort()).toEqual([...ALL_PHASES].sort());
  });

  it('allows every documented legal transition', () => {
    for (const from of ALL_PHASES) {
      for (const to of LEGAL[from]) {
        expect(canTransition(from, to), `${from} → ${to}`).toBe(true);
        expect(transitionPhase(from, to)).toBe(to);
      }
    }
  });

  it('rejects every undocumented edge', () => {
    for (const from of ALL_PHASES) {
      const allowed = new Set(LEGAL[from]);
      for (const to of ALL_PHASES) {
        if (allowed.has(to)) continue;
        expect(canTransition(from, to), `${from} → ${to} should be illegal`).toBe(false);
        expect(() => transitionPhase(from, to)).toThrowError(/illegal_playback_transition/);
      }
    }
  });

  it('error message names the illegal transition', () => {
    expect(() => transitionPhase('error', 'paused')).toThrowError(
      /illegal_playback_transition.*error.*paused/,
    );
  });

  it('flagsFromPhase: only playing has isPlaying=true', () => {
    for (const phase of ALL_PHASES) {
      const flags = flagsFromPhase(phase);
      expect(flags.isPlaying, phase).toBe(phase === 'playing');
    }
  });

  it('flagsFromPhase: loading phases set isLoading', () => {
    expect(flagsFromPhase('resolving').isLoading).toBe(true);
    expect(flagsFromPhase('loading').isLoading).toBe(true);
    expect(flagsFromPhase('recovering').isLoading).toBe(true);
    expect(flagsFromPhase('playing').isLoading).toBe(false);
    expect(flagsFromPhase('paused').isLoading).toBe(false);
    expect(flagsFromPhase('idle').isLoading).toBe(false);
    expect(flagsFromPhase('error').isLoading).toBe(false);
  });
});

/**
 * Phase 3 regression tests: phase is the single source of truth.
 *
 * These tests were written RED to drive the R1 + R2 fixes:
 *  - R1: `patchPlayerState` must derive isPlaying/isLoading from playbackPhase,
 *        never let a stale flag from the patch object stick.
 *  - R2: `initPlayer` HMR reuse must set playbackPhase (not just isPlaying)
 *        so phase and flags stay consistent.
 *
 * Fix commit: `refactor(player): derive UI state from playback phase`.
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn() }));
vi.stubGlobal('AudioContext', undefined);
vi.stubGlobal('webkitAudioContext', undefined);

import {
  playerStore,
  initPlayer,
  __resetWebAudioEqForTests,
  __resetPlaybackCoordinatorForTests,
  __patchPlayerStateForTests,
} from '../playerStore';
import { __resetFmSessionForTests } from '../fmSession';

function resetStore() {
  playerStore.queue = [];
  playerStore.currentIndex = -1;
  playerStore.queueMode = 'normal';
  playerStore.currentTrack = null;
  playerStore.isPlaying = false;
  playerStore.isLoading = false;
  playerStore.currentTime = 0;
  playerStore.duration = 0;
  playerStore.errorMsg = '';
  playerStore.playbackPhase = 'idle';
  (playerStore as any).audio = null;
  (window as any).__bottlemusic_audio__ = undefined;
  (window as any).__bottlemusic_player_cleanup__ = undefined;
  __resetWebAudioEqForTests();
  __resetPlaybackCoordinatorForTests();
  __resetFmSessionForTests();
}

describe('R1: patchPlayerState derives flags from phase (not patch)', () => {
  beforeEach(() => {
    resetStore();
  });

  it('strips isPlaying=true from patch when phase=paused (flag must derive from phase)', () => {
    playerStore.playbackPhase = 'idle';
    // Caller erroneously passes isPlaying=true alongside phase=paused.
    // The patcher must ignore the stale flag and derive isPlaying=false from phase.
    __patchPlayerStateForTests({
      isPlaying: true,
      isLoading: true,
      playbackPhase: 'paused',
    });

    expect(playerStore.playbackPhase).toBe('paused');
    expect(playerStore.isPlaying).toBe(false); // paused → false
    expect(playerStore.isLoading).toBe(false); // paused → false
  });

  it('strips isLoading=true from patch when phase=playing', () => {
    playerStore.playbackPhase = 'idle';
    __patchPlayerStateForTests({
      isLoading: true,
      isPlaying: false,
      playbackPhase: 'playing',
    });

    expect(playerStore.playbackPhase).toBe('playing');
    expect(playerStore.isPlaying).toBe(true); // playing → true
    expect(playerStore.isLoading).toBe(false); // playing → false
  });

  it('derives isLoading=true when phase=resolving|loading|recovering', () => {
    playerStore.playbackPhase = 'idle';
    __patchPlayerStateForTests({ playbackPhase: 'resolving' });
    expect(playerStore.isLoading).toBe(true);
    expect(playerStore.isPlaying).toBe(false);

    __patchPlayerStateForTests({ playbackPhase: 'loading' });
    expect(playerStore.isLoading).toBe(true);
    expect(playerStore.isPlaying).toBe(false);

    __patchPlayerStateForTests({ playbackPhase: 'recovering' });
    expect(playerStore.isLoading).toBe(true);
    expect(playerStore.isPlaying).toBe(false);
  });

  it('does not touch isPlaying/isLoading when patch has no phase', () => {
    // Non-phase patches (position, duration, errorMsg, etc.) must not reset flags.
    playerStore.playbackPhase = 'playing';
    playerStore.isPlaying = true;
    playerStore.isLoading = false;

    __patchPlayerStateForTests({ currentTime: 42, duration: 200, errorMsg: '' });

    expect(playerStore.isPlaying).toBe(true); // unchanged
    expect(playerStore.isLoading).toBe(false); // unchanged
    expect(playerStore.currentTime).toBe(42);
    expect(playerStore.duration).toBe(200);
  });
});

describe('R2: initPlayer HMR reuse derives phase from audio state', () => {
  beforeEach(() => {
    resetStore();
  });
  afterEach(() => vi.useRealTimers());

  it('sets playbackPhase=playing (not just isPlaying) when reused audio is mid-play', () => {
    const oldAudio = document.createElement('audio') as HTMLAudioElement;
    oldAudio.src = 'http://127.0.0.1:17631/audio/hmr-playing';
    Object.defineProperty(oldAudio, 'paused', { value: false, configurable: true });
    Object.defineProperty(oldAudio, 'ended', { value: false, configurable: true });

    (window as any).__bottlemusic_audio__ = oldAudio;
    (playerStore as any).audio = null;
    playerStore.isPlaying = false;
    playerStore.playbackPhase = 'idle';

    initPlayer();

    expect(playerStore.audio).toBe(oldAudio);
    expect(playerStore.isPlaying).toBe(true);
    // R2: phase must be set, not just isPlaying — phase is the source of truth.
    expect(playerStore.playbackPhase).toBe('playing');
  });

  it('sets playbackPhase=paused when reused audio is paused and a track is restored', () => {
    const oldAudio = document.createElement('audio') as HTMLAudioElement;
    oldAudio.src = 'http://127.0.0.1:17631/audio/hmr-paused';
    Object.defineProperty(oldAudio, 'paused', { value: true, configurable: true });
    Object.defineProperty(oldAudio, 'ended', { value: false, configurable: true });

    // Simulate a restored queue so there IS a current track to be paused.
    const track = { FileHash: 'h1', SongName: 'A' } as any;
    playerStore.queue = [track];
    playerStore.currentIndex = 0;
    playerStore.currentTrack = track;

    (window as any).__bottlemusic_audio__ = oldAudio;
    (playerStore as any).audio = null;
    playerStore.isPlaying = true;
    playerStore.playbackPhase = 'idle';

    initPlayer();

    expect(playerStore.audio).toBe(oldAudio);
    expect(playerStore.isPlaying).toBe(false);
    expect(playerStore.playbackPhase).toBe('paused');
  });

  it('leaves playbackPhase=idle when reused audio is paused but no track is restored', () => {
    const oldAudio = document.createElement('audio') as HTMLAudioElement;
    oldAudio.src = '';
    Object.defineProperty(oldAudio, 'paused', { value: true, configurable: true });
    Object.defineProperty(oldAudio, 'ended', { value: false, configurable: true });

    (window as any).__bottlemusic_audio__ = oldAudio;
    (playerStore as any).audio = null;
    playerStore.queue = [];
    playerStore.currentIndex = -1;
    playerStore.currentTrack = null;
    playerStore.playbackPhase = 'idle';

    initPlayer();

    // No track → no phase transition; stays idle.
    expect(playerStore.playbackPhase).toBe('idle');
    expect(playerStore.isPlaying).toBe(false);
  });
});

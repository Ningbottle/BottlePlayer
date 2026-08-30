/**
 * Playback public API (C1 transitional facade).
 *
 * The single surface UI features and the composition root are allowed to
 * consume: reactive state projection, lifecycle entry points, play commands,
 * and user-facing EQ commands. Backend/runtime internals and test-only seams
 * are deliberately NOT exported here (plan Task C1).
 *
 * Implementation still lives in ../api/*; it moves under playback/ in later
 * Phase C tasks.
 */
import {
  playerStore,
  eqState,
  initPlayer,
  initPlayerBackend,
  disposePlayerRuntime,
  playTrack,
  setQuality,
  togglePlay,
  next,
  prev,
  seek,
  setVolume,
  playAll,
  playPersonalFm,
  addToQueue,
  removeFromQueue,
  clearQueue,
  setWebAudioEqBand,
  setWebAudioEqEnabled,
  retryEq,
} from '../api/playerStore';
import type {
  Track,
  LoopMode,
  QueueMode,
  QualityOption,
} from './types';

export {
  // State projection
  playerStore,
  eqState,
  // Lifecycle
  initPlayer,
  initPlayerBackend,
  disposePlayerRuntime,
  // Playback commands
  playTrack,
  setQuality,
  togglePlay,
  next,
  prev,
  seek,
  setVolume,
  playAll,
  playPersonalFm,
  addToQueue,
  removeFromQueue,
  clearQueue,
  // UI EQ commands
  setWebAudioEqBand,
  setWebAudioEqEnabled,
  retryEq,
};
export type { Track, LoopMode, QueueMode, QualityOption };

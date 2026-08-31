/**
 * Playback public API — the single surface UI features and the composition
 * root are allowed to consume.
 *
 * Exported: reactive state projection, lifecycle entry points, play commands,
 * user-facing EQ state/commands + config, diagnostics read model, the
 * recent-played read model, player sync public commands/types, the OS media
 * bridge lifecycle, the cover flight animation, and the UI-safe audio-level
 * adapter.
 *
 * Deliberately NOT exported: Backend instances, the Coordinator, MediaRuntime
 * itself, the raw <audio> element, EQ WebAudio nodes, or any test-only seam.
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
} from './playerStore';
import {
  recentPlayedStore,
  type RecentPlayedEntry,
} from './data/recentPlayedStore';
import { playbackDiagnostics } from './playbackDiagnostics';
import {
  EQ_BANDS,
  EQ_PRESET_LABELS,
  EQ_PRESETS,
  normalizeEqBands,
} from './eq/equalizerConfig';
import {
  onPlayerState,
  sendPlayerCommand,
  applySyncedTheme,
  pinOverlayThemeDark,
  startPlayerSyncHost,
  type PlayerSyncState,
  type PlayerCommand,
} from './sync/playerSync';
import {
  bindOsMediaBridge,
  unbindOsMediaBridge,
} from './sync/osMediaBridge';
import { flyCoverToDock } from './components/coverFlight';
import { createPlaybackAudioLevelMonitor } from './audioLevel';
import type { AudioLevelMonitor } from './audioLevel';
import type { PlaybackPhase } from './playbackPhase';
import type { DiagEvent } from './playbackDiagnostics';
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
  // EQ configuration (pure, UI-facing)
  EQ_BANDS,
  EQ_PRESET_LABELS,
  EQ_PRESETS,
  normalizeEqBands,
  // Diagnostics read model
  playbackDiagnostics,
  // Recent-played read model
  recentPlayedStore,
  // Player sync (overlay views) — public commands/types + host lifecycle
  onPlayerState,
  sendPlayerCommand,
  applySyncedTheme,
  pinOverlayThemeDark,
  startPlayerSyncHost,
  // OS media bridge lifecycle (composition root)
  bindOsMediaBridge,
  unbindOsMediaBridge,
  // Cover flight animation (home)
  flyCoverToDock,
  // UI-safe audio level adapter (home atmosphere)
  createPlaybackAudioLevelMonitor,
};
export type {
  Track,
  LoopMode,
  QueueMode,
  QualityOption,
  PlaybackPhase,
  DiagEvent,
  RecentPlayedEntry,
  PlayerSyncState,
  PlayerCommand,
  AudioLevelMonitor,
};

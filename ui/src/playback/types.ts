/**
 * Playback type facade (C1). Type-only re-exports of the types UI consumers
 * need, still owned by their legacy api/ modules until Phase C moves them.
 * No runtime import is allowed in this file (pure type surface).
 */
import type { Track } from '../api/normalizer';
import type { LoopMode, QueueMode } from '../api/playerStore';
import type { QualityOption } from '../api/playbackOrchestrator';

export type { Track };
export type { LoopMode };
export type { QueueMode };
export type { QualityOption };

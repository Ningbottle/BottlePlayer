/**
 * Playback type facade. Public playback types UI consumers are allowed to use.
 * Track/LoopMode/QueueMode/QualityOption are type-only re-exports still owned
 * by their legacy api/ modules; ResolveTrackResult has its single actual
 * definition here (moved from playbackOrchestrator.ts in C3).
 */
import type { Track } from '../api/normalizer';
import type { LoopMode, QueueMode } from './playerStore';
import type { QualityOption } from '../api/playbackOrchestrator';

export type { Track };
export type { LoopMode };
export type { QueueMode };
export type { QualityOption };

export interface ResolveTrackResult {
  status: number;
  url?: string;
  error?: string;
  is_preview?: boolean;
  vip_required?: boolean;
  data?: {
    available_qualities?: QualityOption[];
    [key: string]: unknown;
  };
}

/**
 * Playback type facade. Public playback types UI consumers are allowed to use.
 * Track/LoopMode/QueueMode/QualityOption are type-only re-exports still owned
 * by their legacy api/ modules; ResolveTrackResult has its single actual
 * definition here (moved from playbackOrchestrator.ts in C3).
 */
import type { Track } from '../shared/music/track';
import type { LoopMode, QueueMode } from './playerStore';
import type { QualityOption } from './runtime/playbackOrchestrator';

export type { Track };
export type { LoopMode };
export type { QueueMode };
export type { QualityOption };
export type Delivery = 'full' | 'preview' | 'unknown';

export interface SongUrlAttempt {
  endpoint: 'v6' | 'v5-main' | 'v5-preview' | 'v5-anon';
  http_status: number;
  errcode: number;
  fail_process: unknown;
  anonymous: boolean;
  quality: string;
  delivery: Delivery;
  is_preview: boolean;
  error: string;
  /** Sanitized upstream message (e.g. "token api error."); never a signed URL. */
  message?: string;
  /**
   * Native reject classification for this attempt:
   * auth_rejected | entitlement_required | transport | http | invalid_json | upstream_error
   */
  reject_class?: string;
}

export interface ResolveTrackResult {
  status: number;
  url?: string;
  error?: string;
  error_code?: string;
  is_preview?: boolean;
  delivery?: Delivery;
  attempts?: SongUrlAttempt[];
  vip_required?: boolean;
  data?: {
    available_qualities?: QualityOption[];
    [key: string]: unknown;
  };
}

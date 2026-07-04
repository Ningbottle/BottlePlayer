export type DiagKind =
  | 'track_switch'
  | 'url_resolve'
  | 'media_event'
  | 'proxy_prep'
  | 'fm_fetch'
  | 'potential_stall';

export type DiagPhase = 'start' | 'ok' | 'fail' | 'noop';

export interface DiagEvent {
  ts: number;
  kind: DiagKind;
  phase: DiagPhase;
  detail: string;
  trackKey?: string;
}

export interface PlaybackDiagnosticsOptions {
  now?: () => number;
  capacity?: number;
}

const DEFAULT_CAPACITY = 200;
const STALL_THRESHOLD_MS = 5000;

export class PlaybackDiagnostics {
  private buffer: DiagEvent[] = [];
  private readonly now: () => number;
  private readonly capacity: number;
  private stallTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(opts: PlaybackDiagnosticsOptions = {}) {
    this.now = opts.now ?? Date.now;
    this.capacity = opts.capacity ?? DEFAULT_CAPACITY;
  }

  recordEvent(e: Omit<DiagEvent, 'ts'>): void {
    this.buffer.unshift({ ...e, ts: this.now() });
    if (this.buffer.length > this.capacity) {
      // Drop oldest (at the end, since newest is at index 0).
      this.buffer.length = this.capacity;
    }
    // Arm the stall detector on stall-like media events (waiting/stalled/suspend/abort).
    // If no markActivity (play/timeupdate) arrives within 5s, flag a potential_stall.
    if (e.kind === 'media_event' && e.phase === 'noop') {
      this.armStallDetector();
    }
  }

  /** Signal playback activity (play/timeupdate). Clears any pending stall flag. */
  markActivity(): void {
    if (this.stallTimer) {
      clearTimeout(this.stallTimer);
      this.stallTimer = null;
    }
  }

  private armStallDetector(): void {
    if (this.stallTimer) clearTimeout(this.stallTimer);
    this.stallTimer = setTimeout(() => {
      this.stallTimer = null;
      this.buffer.unshift({
        kind: 'potential_stall',
        phase: 'noop',
        detail: 'no activity (timeupdate/play) for 5s after a stall-like media event',
        ts: this.now(),
      });
      if (this.buffer.length > this.capacity) {
        this.buffer.length = this.capacity;
      }
    }, STALL_THRESHOLD_MS);
  }

  getEvents(): DiagEvent[] {
    return [...this.buffer];
  }

  copyAsText(): string {
    return this.buffer
      .map((e) => {
        const base = `${e.ts} ${e.kind} ${e.phase}: ${e.detail}`;
        return e.trackKey ? `${base} [${e.trackKey}]` : base;
      })
      .join('\n');
  }

  reset(): void {
    if (this.stallTimer) {
      clearTimeout(this.stallTimer);
      this.stallTimer = null;
    }
    this.buffer = [];
  }
}

export const playbackDiagnostics = new PlaybackDiagnostics();

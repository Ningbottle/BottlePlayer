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

export class PlaybackDiagnostics {
  private buffer: DiagEvent[] = [];
  private readonly now: () => number;
  private readonly capacity: number;

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
    this.buffer = [];
  }
}

export const playbackDiagnostics = new PlaybackDiagnostics();

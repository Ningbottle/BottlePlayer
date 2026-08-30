import type { Track } from '../api/normalizer';

/**
 * A play-stats record to send to the native stats service.
 */
export interface PlayRecord {
  song_hash: string;
  song_name: string;
  singer_name: string;
  album_id: string;
  album_name: string;
  cover_url: string;
  duration_seconds: number;
  completed: boolean;
  listened_seconds: number;
  quality: string;
  played_at: number;
}

/** Emits a finalized play record. Implementations are fire-and-forget. */
export type RecordEmitter = (record: PlayRecord) => void;

/** Provides the current quality string at record time (read live). */
export type QualityProvider = () => string;

/** Provides a timestamp (ms) for played_at. Abstracted for testability. */
export type NowProvider = () => number;

/** A single timeupdate delta larger than this is treated as a seek, not play. */
const SEEK_THRESHOLD = 2; // seconds
const MIN_RECORD_LISTENED_SECONDS = 60;

type Phase = 'idle' | 'pending' | 'playing' | 'paused';

interface Session {
  track: Track;
  listened: number; // accumulated real-played seconds
  lastTime: number | null; // last onTimeUpdate position, for delta accumulation
  phase: Phase;
  finalized: boolean;
}

/**
 * Tracks play statistics for the currently-playing track as an event-driven
 * state machine. Session recording is gated on a real `play` event so a
 * rejected play() (e.g. autoplay block, broken src) never opens a ghost
 * session.
 *
 * listened_seconds is accumulated from timeupdate deltas, so seek / loop
 * restart / background-suspend cannot inflate it: only forward increments
 * smaller than SEEK_THRESHOLD count; large jumps (seek) and backward jumps
 * (replay) are ignored.
 *
 * The tracker is pure logic — it emits records via `emit` and reads quality
 * via `qualityProvider`, so it has no dependency on Tauri or the DOM and is
 * fully unit-testable.
 */
export class PlaySessionTracker {
  private session: Session | null = null;

  constructor(
    private readonly emit: RecordEmitter,
    private readonly qualityProvider: QualityProvider,
    private readonly now: NowProvider,
  ) {}

  /** A track is about to start; finalize any in-progress session first. */
  intend(track: Track): void {
    if (this.session && !this.session.finalized) {
      this.finalize(/* completed */ false);
    }
    this.session = {
      track,
      listened: 0,
      lastTime: null,
      phase: 'pending',
      finalized: false,
    };
  }

  /** The audio element actually started playing (the real `play` event). */
  onPlay(): void {
    if (!this.session) return;
    // If a previously-finalized session is being resumed (replay), open a new
    // session for the same track so the second listen is recorded.
    if (this.session.finalized) {
      const track = this.session.track;
      this.session = {
        track,
        listened: 0,
        lastTime: null,
        phase: 'playing',
        finalized: false,
      };
      return;
    }
    this.session.phase = 'playing';
  }

  /** Playback paused. */
  onPause(): void {
    if (this.session && !this.session.finalized) {
      this.session.phase = 'paused';
    }
  }

  /** Accumulate real-played time from a timeupdate position (seconds). */
  onTimeUpdate(currentTime: number): void {
    if (!this.session || this.session.finalized) return;
    if (this.session.phase !== 'playing') return;
    const last = this.session.lastTime;
    if (last !== null) {
      const delta = currentTime - last;
      // Only count small forward increments. Large jumps = seek; backward = replay.
      if (delta > 0 && delta < SEEK_THRESHOLD) {
        this.session.listened += delta;
      }
    }
    this.session.lastTime = currentTime;
  }

  /** Track reached its natural end → record as completed. */
  onEnded(): void {
    if (!this.session || this.session.finalized) return;
    this.finalize(/* completed */ true);
  }

  /** User skipped / switched away → record as incomplete (if started). */
  skip(): void {
    if (!this.session || this.session.finalized) return;
    // Only record sessions that actually started playing.
    if (this.session.phase === 'pending' || this.session.phase === 'idle') {
      this.session.finalized = true;
      this.session.phase = 'idle';
      return;
    }
    this.finalize(/* completed */ false);
  }

  private finalize(completed: boolean): void {
    if (!this.session || this.session.finalized) return;
    this.session.finalized = true;
    this.session.phase = 'idle';
    const listenedSeconds = Math.max(0, Math.round(this.session.listened));
    if (listenedSeconds <= MIN_RECORD_LISTENED_SECONDS) return;

    const record: PlayRecord = {
      song_hash: this.session.track.FileHash || '',
      song_name: this.session.track.SongName || '',
      singer_name: this.session.track.SingerName || '',
      album_id: this.session.track.AlbumID || '',
      album_name: this.session.track.AlbumName || '',
      cover_url: this.session.track.Image || '',
      duration_seconds: this.session.track.Duration || 0,
      completed,
      listened_seconds: listenedSeconds,
      quality: this.qualityProvider() || '',
      played_at: this.now(),
    };
    this.emit(record);
  }
}

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { PlaySessionTracker } from '../playSessionTracker';
import type { Track } from '../../shared/music/track';

function mkTrack(partial: Partial<Track> = {}): Track {
  return {
    FileHash: 'hash-A',
    SongName: 'Song A',
    SingerName: 'Artist A',
    Duration: 200,
    ...partial,
  };
}

/** Collect records the tracker would emit to the stats service. */
function withCollector() {
  const emitted: any[] = [];
  const tracker = new PlaySessionTracker((rec) => emitted.push(rec), () => 'flac', () => Date.now());
  return { tracker, emitted };
}

describe('PlaySessionTracker', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-06-25T00:00:00Z'));
  });
  afterEach(() => vi.useRealTimers());

  it('does not record a session that never started playing', () => {
    const { tracker, emitted } = withCollector();
    // intend then skip without an onPlay — e.g. user clicked a song that
    // failed to load. No ghost record should be emitted.
    tracker.intend(mkTrack());
    tracker.skip();
    expect(emitted).toHaveLength(0);
  });

  it('records a completed play with listened_seconds near full duration', () => {
    const { tracker, emitted } = withCollector();
    const track = mkTrack({ FileHash: 'hash-B', Duration: 200 });
    tracker.intend(track);
    tracker.onPlay();
    // simulate playback advancing via timeupdate ticks
    for (let t = 0; t <= 199; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded();

    expect(emitted).toHaveLength(1);
    expect(emitted[0].song_hash).toBe('hash-B');
    expect(emitted[0].completed).toBe(true);
    // accumulator should be ~199s, not 0
    expect(emitted[0].listened_seconds).toBeGreaterThan(195);
  });

  it('records an incomplete play with accumulated listened_seconds on skip', () => {
    const { tracker, emitted } = withCollector();
    const track = mkTrack({ FileHash: 'hash-C' });
    tracker.intend(track);
    tracker.onPlay();
    for (let t = 0; t <= 75; t += 0.25) tracker.onTimeUpdate(t);
    tracker.skip();

    expect(emitted).toHaveLength(1);
    expect(emitted[0].completed).toBe(false);
    expect(emitted[0].listened_seconds).toBeGreaterThan(73);
    expect(emitted[0].listened_seconds).toBeLessThan(77);
  });

  it('does not record plays listened for one minute or less', () => {
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'short' }));
    tracker.onPlay();
    for (let t = 0; t <= 60; t += 0.25) tracker.onTimeUpdate(t);
    tracker.skip();

    expect(emitted).toHaveLength(0);
  });

  it('records plays listened for more than one minute', () => {
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'long-enough' }));
    tracker.onPlay();
    for (let t = 0; t <= 61; t += 0.25) tracker.onTimeUpdate(t);
    tracker.skip();

    expect(emitted).toHaveLength(1);
    expect(emitted[0].song_hash).toBe('long-enough');
    expect(emitted[0].listened_seconds).toBeGreaterThan(60);
  });

  it('does not inflate listened_seconds when seeking forward then skipping', () => {
    // The #5 bug: seek() writes currentTime directly, then skip reads it.
    // The accumulator must ignore the large jump.
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'seek' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    // user drags progress bar to near the end — a 220s jump in one tick
    tracker.onTimeUpdate(230);
    tracker.skip();

    expect(emitted).toHaveLength(1);
    expect(emitted[0].listened_seconds).toBeGreaterThan(65);
    expect(emitted[0].listened_seconds).toBeLessThan(75); // ~70s, NOT 230
  });

  it('does not count a backward seek (replay) as listened time', () => {
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack());
    tracker.onPlay();
    for (let t = 0; t <= 50; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onTimeUpdate(5); // seek back to 5s
    for (let t = 5; t <= 25; t += 0.25) tracker.onTimeUpdate(t);
    tracker.skip();

    // 50s forward + 20s after the back-seek = 70s, NOT 50+20+50
    expect(emitted[0].listened_seconds).toBeGreaterThan(66);
    expect(emitted[0].listened_seconds).toBeLessThan(74);
  });

  it('records each track once when playing a sequence to completion', () => {
    const { tracker, emitted } = withCollector();
    // Track 1
    tracker.intend(mkTrack({ FileHash: 'h1' }));
    tracker.onPlay();
    for (let t = 0; t <= 100; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded(); // -> finalizes h1 completed, starts nothing
    // Track 2
    tracker.intend(mkTrack({ FileHash: 'h2' }));
    tracker.onPlay();
    for (let t = 0; t <= 100; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded();

    expect(emitted).toHaveLength(2);
    expect(emitted[0].song_hash).toBe('h1');
    expect(emitted[1].song_hash).toBe('h2');
    expect(emitted.map((r) => r.completed)).toEqual([true, true]);
  });

  it('skips the current incomplete track when intending a new one', () => {
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'old' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    // user clicks a different song while old is still playing
    tracker.intend(mkTrack({ FileHash: 'new' }));

    expect(emitted).toHaveLength(1);
    expect(emitted[0].song_hash).toBe('old');
    expect(emitted[0].completed).toBe(false);
  });

  it('does not emit a ghost session when onPlay never fires (play rejected)', () => {
    // The #6 single-loop ghost: handleEnded called startPlaySession after a
    // play() that rejected. intend alone must not open a recordable session.
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'ghost' }));
    // play() rejected — no onPlay
    tracker.skip();
    expect(emitted).toHaveLength(0);
  });

  it('reopens a new session when resuming after a track already ended', () => {
    // The #7 togglePlay miss: a track ended (recorded), user hits play to
    // hear it again — that second listen must be recorded.
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'resume' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded();
    expect(emitted).toHaveLength(1);

    // user resumes the same track from the start
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded();
    expect(emitted).toHaveLength(2);
    expect(emitted[1].song_hash).toBe('resume');
    expect(emitted[1].completed).toBe(true);
  });

  it('records the old quality session then starts a new one on quality switch', () => {
    // The #8 setQuality bypass: switching quality mid-stream must finalize
    // the old (incomplete) session and begin a fresh one.
    const { tracker, emitted } = withCollector();
    tracker.intend(mkTrack({ FileHash: 'q' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);

    // quality switch: skip + intend same track
    tracker.skip();
    tracker.intend(mkTrack({ FileHash: 'q' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    tracker.onEnded();

    expect(emitted).toHaveLength(2);
    expect(emitted[0].completed).toBe(false);
    expect(emitted[1].completed).toBe(true);
  });

  it('reads quality live from the provider at record time', () => {
    let quality = '128';
    const emitted: any[] = [];
    const tracker = new PlaySessionTracker(
      (rec) => emitted.push(rec),
      () => quality,
      () => Date.now(),
    );
    tracker.intend(mkTrack({ FileHash: 'ql' }));
    tracker.onPlay();
    for (let t = 0; t <= 70; t += 0.25) tracker.onTimeUpdate(t);
    quality = 'flac'; // user switched quality during playback
    tracker.onEnded();

    expect(emitted[0].quality).toBe('flac');
  });
});

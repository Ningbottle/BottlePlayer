import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { PlaybackDiagnostics } from '../playbackDiagnostics';

function mkStore(opts: { now?: () => number; capacity?: number } = {}) {
  return new PlaybackDiagnostics({
    now: opts.now ?? (() => 0),
    capacity: opts.capacity,
  });
}

describe('PlaybackDiagnostics', () => {
  it('records an event with a timestamp and returns it via getEvents', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'track_switch', phase: 'start', detail: 'switched to h1' });

    const events = store.getEvents();
    expect(events).toHaveLength(1);
    expect(events[0]).toEqual({
      ts: 1000,
      kind: 'track_switch',
      phase: 'start',
      detail: 'switched to h1',
    });
  });

  it('evicts the oldest event when capacity is exceeded (FIFO, newest-first)', () => {
    let now = 0;
    const store = mkStore({ now: () => now, capacity: 3 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'first' });
    now++;
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'second' });
    now++;
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'third' });
    now++;
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'fourth' });
    now++;

    const events = store.getEvents();
    expect(events).toHaveLength(3);
    // newest-first: fourth, third, second — "first" evicted as oldest
    expect(events.map((e) => e.detail)).toEqual(['fourth', 'third', 'second']);
  });

  it('copyAsText produces compact multi-line text (newest-first), one event per line', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'track_switch', phase: 'start', detail: 'switched to h1', trackKey: 'h1' });
    store.recordEvent({ kind: 'url_resolve', phase: 'ok', detail: 'resolved h1' });

    const text = store.copyAsText();
    const lines = text.split('\n').filter((l) => l.length > 0);
    expect(lines).toHaveLength(2);
    // newest-first
    expect(lines[0]).toContain('url_resolve');
    expect(lines[0]).toContain('ok');
    expect(lines[0]).toContain('resolved h1');
    expect(lines[1]).toContain('track_switch');
    expect(lines[1]).toContain('h1'); // trackKey appears
  });

  it('reset clears the buffer', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'x' });
    expect(store.getEvents()).toHaveLength(1);
    store.reset();
    expect(store.getEvents()).toHaveLength(0);
    expect(store.copyAsText()).toBe('');
  });

  it('copyAsText on an empty buffer returns an empty string', () => {
    const store = mkStore();
    expect(store.copyAsText()).toBe('');
  });

  it('redacts URL query strings before events enter the copyable buffer', () => {
    const store = mkStore();
    store.recordEvent({
      kind: 'url_resolve',
      phase: 'ok',
      detail: 'https://cdn.example/song.mp3?token=secret&expires=123',
    });

    const detail = store.getEvents()[0].detail;
    expect(detail).toContain('https://cdn.example/song.mp3?[redacted]');
    expect(detail).not.toContain('token=secret');
    expect(store.copyAsText()).not.toContain('expires=123');
  });

  it('getEvents returns a defensive copy; mutating the result does not affect the store', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'x' });

    const events = store.getEvents();
    events.pop();
    events.push({ ts: 9999, kind: 'potential_stall', phase: 'fail', detail: 'injected' });

    const fresh = store.getEvents();
    expect(fresh).toHaveLength(1);
    expect(fresh[0].detail).toBe('x');
  });

  it('keeps all events when buffer is exactly at capacity (no eviction)', () => {
    let now = 0;
    const store = mkStore({ now: () => now, capacity: 3 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'a' });
    now++;
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'b' });
    now++;
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'c' });

    const events = store.getEvents();
    expect(events).toHaveLength(3);
    expect(events.map((e) => e.detail)).toEqual(['c', 'b', 'a']);
  });

  it('reset-then-record: buffer is usable again after reset', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'before' });
    store.reset();
    expect(store.getEvents()).toHaveLength(0);

    store.recordEvent({ kind: 'track_switch', phase: 'start', detail: 'after' });
    const events = store.getEvents();
    expect(events).toHaveLength(1);
    expect(events[0].detail).toBe('after');
    expect(events[0].ts).toBe(1000);
  });

  it('recordEvent without trackKey produces a line with no [trackKey] suffix', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordEvent({ kind: 'url_resolve', phase: 'ok', detail: 'resolved h1' });

    const line = store.copyAsText();
    expect(line).toBe('1000 url_resolve ok: resolved h1');
    expect(line).not.toContain('[');
  });

  it('default capacity (200) holds exactly 200 events; the 201st evicts the oldest', () => {
    let now = 0;
    const store = mkStore({ now: () => now });
    for (let i = 0; i < 200; i++) {
      store.recordEvent({ kind: 'media_event', phase: 'noop', detail: `e${i}` });
      now++;
    }
    expect(store.getEvents()).toHaveLength(200);

    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'overflow' });
    const events = store.getEvents();
    expect(events).toHaveLength(200);
    expect(events[0].detail).toBe('overflow'); // newest first
    expect(events[events.length - 1].detail).toBe('e1'); // e0 evicted as oldest
  });
});

describe('PlaybackDiagnostics stall auto-flag', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('flags a potential_stall after 5s of no activity following a stalled event', () => {
    const store = mkStore({ capacity: 10 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'stalled' });

    vi.advanceTimersByTime(4999);
    expect(store.getEvents().filter((e) => e.kind === 'potential_stall')).toHaveLength(0);

    vi.advanceTimersByTime(1);
    const stalls = store.getEvents().filter((e) => e.kind === 'potential_stall');
    expect(stalls).toHaveLength(1);
  });

  it('does not flag a potential_stall when markActivity arrives within 5s', () => {
    const store = mkStore({ capacity: 10 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'waiting' });

    vi.advanceTimersByTime(3000);
    store.markActivity(); // playback recovered — clears the stall timer
    vi.advanceTimersByTime(6000); // well past 5s since the stalled event

    expect(store.getEvents().filter((e) => e.kind === 'potential_stall')).toHaveLength(0);
  });

  it('does not arm the stall detector for non-stall media events (error/ended)', () => {
    const store = mkStore({ capacity: 10 });
    store.recordEvent({ kind: 'media_event', phase: 'fail', detail: 'error' });
    store.recordEvent({ kind: 'media_event', phase: 'ok', detail: 'ended' });
    vi.advanceTimersByTime(10000);

    expect(store.getEvents().filter((e) => e.kind === 'potential_stall')).toHaveLength(0);
  });

  it('does not arm the stall detector for abort or suspend events', () => {
    const store = mkStore({ capacity: 10 });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'HTML5 media abort' });
    store.recordEvent({ kind: 'media_event', phase: 'noop', detail: 'HTML5 media suspend' });
    vi.advanceTimersByTime(10000);

    expect(store.getEvents().filter((e) => e.kind === 'potential_stall')).toHaveLength(0);
  });
});

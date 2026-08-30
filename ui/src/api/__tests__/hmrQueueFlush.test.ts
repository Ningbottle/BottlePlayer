/**
 * HMR queue flush regression test.
 *
 * Reproduces the race the reviewer flagged:
 *  1. Old module is playing track B (in-memory queue = [B]).
 *  2. saveQueue() schedules a 500ms debounce — localStorage is still stale ([A] or []).
 *  3. HMR fires within that 500ms window.
 *  4. New module evaluates: playerStore.queue reads STALE localStorage.
 *  5. initPlayer() calls old cleanup → must SYNC-flush the queue to localStorage.
 *  6. initPlayer() must then RE-READ queue from localStorage so the new module
 *     picks up the flushed data (track B), not the stale snapshot.
 *
 * Without the fix, the new module's UI shows track A while the <audio> keeps
 * playing track B — an inconsistent state.
 *
 * This test uses vi.resetModules() + dynamic imports to truly simulate a new
 * module evaluation (not just reusing the same store reference).
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { getMediaRuntime } from '../mediaRuntime';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn() }));
vi.mock('../motion', () => ({ isReducedMotion: vi.fn(() => false) }));
vi.mock('../playbackDiagnostics', () => ({
  playbackDiagnostics: {
    recordEvent: vi.fn(),
    markActivity: vi.fn(),
    reset: vi.fn(),
    getEvents: vi.fn(() => []),
  },
}));
vi.stubGlobal('AudioContext', undefined);
vi.stubGlobal('webkitAudioContext', undefined);

describe('HMR queue flush: no stale snapshot across module reload', () => {
  beforeEach(() => {
    localStorage.clear();
    (window as any).__bottlemusic_media_runtime__ = undefined;
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.resetModules();
  });

  it('flushes pending debounced queue save on HMR dispose before new module reads it', async () => {
    vi.useFakeTimers();

    // ── Phase 1: old module sets up playerStore + queue, triggers saveQueue ──
    const oldStore = await import('../playerStore');
    oldStore.initPlayer(); // binds getSnapshot, registers __bottlemusic_player_cleanup__

    // Simulate: user switched to track B. In-memory queue = [B], but the
    // 500ms debounce means localStorage is still stale.
    const trackB = { FileHash: 'B', SongName: 'B', Duration: 100 } as any;
    oldStore.playerStore.queue = [trackB];
    oldStore.playerStore.currentIndex = 0;

    const { saveQueue } = await import('../playerPersistence');
    saveQueue(); // schedules 500ms debounce timer

    // Confirm localStorage is still stale (debounce hasn't fired). Persistence
    // uses a single atomic snapshot key (player_queue_snapshot).
    expect(localStorage.getItem('player_queue_snapshot')).toBeNull();

    // ── Phase 2: HMR — reset modules, new module evaluates ──
    // The new module's playerStore is created at module-eval time, reading
    // from STALE localStorage. This simulates the real Vite HMR sequence.
    vi.resetModules();
    const newStore = await import('../playerStore');

    // New module's playerStore.queue is stale (empty, from localStorage).
    expect(newStore.playerStore.queue).toHaveLength(0);

    // ── Phase 3: initPlayer calls old cleanup (must flush) + re-reads ──
    newStore.initPlayer();

    // After initPlayer, the new module should have the CORRECT queue (track B).
    // This works because:
    //  1. old cleanupCurrentModuleForHmr() calls flushSaveQueue() → writes [B] to localStorage
    //  2. initPlayer() re-reads queue from localStorage → playerStore.queue = [B]
    expect(newStore.playerStore.queue).toHaveLength(1);
    expect((newStore.playerStore.queue[0] as any)?.FileHash).toBe('B');
    expect(newStore.playerStore.currentIndex).toBe(0);
  });

  it('teardown completes even when localStorage.setItem throws (flush isolated)', async () => {
    // Reviewer concern: if flushSaveQueue throws (quota/permission/WebView),
    // the rest of cleanupCurrentModuleForHmr must still run — otherwise old
    // and new modules' listeners coexist. flushSaveQueue is now isolated in
    // try/catch; this test proves teardown runs past it.
    const oldStore = await import('../playerStore');
    oldStore.initPlayer();

    // Verify the duration listener is wired (set up by initPlayer).
    const audio = getMediaRuntime()!.audio;
    expect(audio).toBeTruthy();
    Object.defineProperty(audio, 'duration', { value: 200, configurable: true, writable: true });
    audio.dispatchEvent(new Event('durationchange'));
    expect(oldStore.playerStore.duration).toBe(200);

    // Reset duration to detect whether the listener fires again later.
    oldStore.playerStore.duration = 0;

    // Make localStorage.setItem throw (quota / permission / WebView error).
    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    // Call cleanup directly — flushSaveQueue is isolated, must not throw.
    const runtime = getMediaRuntime();
    expect(runtime).toBeTruthy();
    expect(() => runtime!.detachForHmr()).not.toThrow();

    // Proof teardown completed PAST flushSaveQueue: the duration listener was
    // removed (MediaRuntime.detachForHmr ran after beforeHmrDetach in the
    // cleanup path). Dispatching durationchange again must NOT update duration.
    Object.defineProperty(audio, 'duration', { value: 300, configurable: true, writable: true });
    audio.dispatchEvent(new Event('durationchange'));
    expect(oldStore.playerStore.duration).toBe(0); // listener removed, no update

    setItemSpy.mockRestore();
  });

  it('locks full UI/audio/phase invariant after HMR (track B everywhere)', async () => {
    vi.useFakeTimers();

    // ── Phase 1: old module playing track B ──
    const oldStore = await import('../playerStore');
    oldStore.initPlayer();

    const trackB = { FileHash: 'B', SongName: 'B', Duration: 100 } as any;
    oldStore.playerStore.queue = [trackB];
    oldStore.playerStore.currentIndex = 0;

    // Simulate audio is playing track B (src set, not paused).
    const audio = getMediaRuntime()!.audio;
    audio.src = 'http://test/B';
    Object.defineProperty(audio, 'paused', { value: false, configurable: true, writable: true });
    Object.defineProperty(audio, 'ended', { value: false, configurable: true, writable: true });

    const { saveQueue } = await import('../playerPersistence');
    saveQueue(); // schedules 500ms debounce

    // ── Phase 2: HMR — reset modules, new module evaluates ──
    vi.resetModules();
    const newStore = await import('../playerStore');

    // New module's queue is stale (empty) before initPlayer.
    expect(newStore.playerStore.queue).toHaveLength(0);

    // ── Phase 3: initPlayer calls old cleanup (flush) + re-reads ──
    newStore.initPlayer();

    // Full invariant: UI B, audio B, phase B — all consistent.
    expect(newStore.playerStore.queue).toHaveLength(1);
    expect((newStore.playerStore.queue[0] as any)?.FileHash).toBe('B');
    expect(newStore.playerStore.currentIndex).toBe(0);
    expect((newStore.playerStore.currentTrack as any)?.FileHash).toBe('B');
    // audio src must NOT be cleared by HMR cleanup.
    expect(getMediaRuntime()!.audio.src).toContain('B');
    // phase must reflect the audio's live playing state.
    expect(newStore.playerStore.playbackPhase).toBe('playing');
    expect(newStore.playerStore.isPlaying).toBe(true);
  });
});

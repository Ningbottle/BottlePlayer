import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

// Mock Tauri invoke (used by stats recording + native_request).
const mockInvoke = vi.fn();
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: any[]) => mockInvoke(...args),
}));

// jsdom has no Web Audio; WebAudioEq's createCtx returns null so EQ is a no-op
// and the <audio> plays directly (the non-CORS degradation path).
vi.stubGlobal('AudioContext', undefined);
vi.stubGlobal('webkitAudioContext', undefined);

import {
  playerStore,
  initPlayer,
  initPlayerBackend,
  playTrack,
  playAll,
  togglePlay,
  __getActiveBackend,
  __getPlaySession,
} from '../playerStore';
import type { Track } from '../normalizer';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'A', Duration: 100 } as Track;
}

/** Reset the playerStore singleton state between tests. */
function resetStore() {
  playerStore.queue = [];
  playerStore.currentIndex = -1;
  playerStore.currentTrack = null;
  playerStore.isPlaying = false;
  playerStore.currentTime = 0;
  playerStore.duration = 0;
  playerStore.errorMsg = '';
  (playerStore as any).audio = null;
  // Clear the zombie-audio sentinel so initPlayer() doesn't run its teardown
  // path (which nulls activeBackend) and skip re-creating the backend.
  (window as any).__bottlemusic_audio__ = undefined;
}

describe('playerStore integration', () => {
  beforeEach(() => {
    localStorage.clear();
    mockInvoke.mockReset();
    resetStore();
  });
  afterEach(() => vi.useRealTimers());

  it('registers exactly one ended listener (no double-fire) and advances once', async () => {
    // #2 root cause: initPlayer registered a DIRECT 'ended' listener AND
    // initPlayerBackend's onEvent registered ANOTHER on the same <audio>.
    // One natural end fired both → next() twice → double /song/url fetch.
    // After rewire, the backend onEvent is the sole 'ended' owner.
    initPlayer();

    // Spy on addEventListener to count 'ended' registrations.
    const audio = (playerStore as any).audio as HTMLAudioElement;
    const addSpy = vi.spyOn(audio, 'addEventListener');
    initPlayerBackend();

    const endedRegs = addSpy.mock.calls.filter((c) => c[0] === 'ended');
    expect(endedRegs).toHaveLength(1); // exactly one 'ended' listener

    // Behavioral check: one ended dispatch → one /song/url fetch for the next track.
    // Tracks carry an Image so fetchCoverImage (another native_request) is skipped,
    // leaving only the /song/url fetch to count.
    const t1 = mkTrack('h1'), t2 = mkTrack('h2'), t3 = mkTrack('h3');
    [t1, t2, t3].forEach((t) => (t.Image = 'http://img/'));
    playerStore.queue = [t1, t2, t3];
    playerStore.currentIndex = 0;
    playerStore.currentTrack = t1;

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/' } });
    });

    // Drive the backend's 'ended' handler directly via a dispatch.
    const fetchesBefore = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
    audio.dispatchEvent(new Event('ended'));

    // The single 'ended' listener calls next() → playTrack(t2) → exactly one fetch.
    await vi.waitFor(() => {
      const fetchesAfter = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
      expect(fetchesAfter - fetchesBefore).toBe(1);
    });
    await new Promise((r) => setTimeout(r, 80));
    const fetchesAfter = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
    expect(fetchesAfter - fetchesBefore).toBe(1);
  });

  it('rolls back currentIndex when /song/url fails so the failed track is not persisted as current', async () => {
    // #13: playTrack persisted currentIndex BEFORE the fetch; on failure the
    // bad track stayed as current and got re-persisted, trapping the user.
    playAll([mkTrack('good')], 0);
    const before = playerStore.currentIndex; // 0
    initPlayerBackend();

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 0, error: 'vip' } });
    });

    await playTrack(mkTrack('bad'));
    expect(playerStore.errorMsg).toBeTruthy();
    expect(playerStore.currentIndex).toBe(before);
  });

  it('does not crash on corrupt localStorage queue JSON at import time', async () => {
    // #14: bare JSON.parse(localStorage) threw at import time on corrupt data,
    // blank-screening the app. loadJSON must swallow and fall back.
    vi.resetModules();
    localStorage.clear();
    localStorage.setItem('player_queue', '{not valid json');
    localStorage.setItem('player_eq_bands', '!!!');

    // Re-importing the module triggers the module-level localStorage parse.
    const mod = await import('../playerStore');
    // Import did not throw, and state fell back to safe defaults.
    expect(mod.playerStore.queue).toEqual([]);
    expect(mod.playerStore.eqBands).toEqual([0, 0, 0, 0, 0]);
  });

  it('calls playSession.intend() on a successful play (session opens for the new track)', async () => {
    // Bug A regression guard: intend() must run on the success path so the
    // session exists before the 'play' event fires. Previously intend() ran
    // after playUrl(), so onPlay() opened the wrong session and listened_seconds
    // stayed 0. The seek-immune accumulation itself is unit-tested in
    // playSessionTracker.test.ts; here we guard that playTrack wires intend().
    initPlayer();
    initPlayerBackend();

    let intendCalled = false;
    const tracker = __getPlaySession();
    const realIntend = tracker.intend.bind(tracker);
    tracker.intend = (t: any) => { intendCalled = true; realIntend(t); };

    // jsdom audio.play() rejects, so patch the backend to report success.
    const { Html5AudioBackend } = await import('../html5Backend');
    const realPlayUrl = Html5AudioBackend.prototype.playUrl;
    Html5AudioBackend.prototype.playUrl = async function () { return true; };

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/song.mp3' } });
    });

    const track = mkTrack('ordering-test');
    track.Image = 'http://img/';
    await playTrack(track);

    Html5AudioBackend.prototype.playUrl = realPlayUrl;
    expect(intendCalled).toBe(true);
  });

  it('togglePlay re-fetches the URL when the audio has no src (resume-after-stop bug)', async () => {
    // Bug: after stop()/init-restore the <audio> can have an empty src but a
    // currentTrack still set. togglePlay→resume()→audio.play() on an empty src
    // rejects AbortError ("play() interrupted by pause()"), leaving the player
    // stuck: the play button toggles isPlaying but no audio plays and the
    // progress bar never moves. togglePlay must detect the empty/unloaded src
    // and re-run playTrack(currentTrack) to load a fresh URL.
    initPlayer();
    initPlayerBackend();

    // Simulate the stuck state: a currentTrack exists but audio.src is empty
    // (as happens after initPlayer restores a track without loading it, or
    // after a stop cleared the src).
    const track = mkTrack('stuck-track');
    track.Image = 'http://img/';
    playerStore.currentTrack = track;
    playerStore.queue = [track];
    playerStore.currentIndex = 0;
    playerStore.isPlaying = false;
    const audio = (playerStore as any).audio as HTMLAudioElement;
    audio.removeAttribute('src');

    // Stub playUrl on the prototype so jsdom's rejecting audio.play() is bypassed.
    const { Html5AudioBackend } = await import('../html5Backend');
    const realPlayUrl = Html5AudioBackend.prototype.playUrl;
    Html5AudioBackend.prototype.playUrl = async function (this: any, url: string) {
      this.audio.src = url;
      return true;
    };
    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fresh.mp3' } });
    });

    await togglePlay();

    Html5AudioBackend.prototype.playUrl = realPlayUrl;
    // togglePlay should have re-loaded via playTrack: the audio now has a real src.
    expect(audio.src, 'togglePlay should re-load the URL via playTrack').toContain('fresh.mp3');
  });
});

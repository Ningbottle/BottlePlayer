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
  __resetEqDisabledForSession,
  eqState,
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
  playerStore.isLoading = false;
  playerStore.currentTime = 0;
  playerStore.duration = 0;
  playerStore.errorMsg = '';
  (playerStore as any).audio = null;
  // Clear the zombie-audio sentinel so initPlayer() doesn't run its teardown
  // path (which nulls activeBackend) and skip re-creating the backend.
  (window as any).__bottlemusic_audio__ = undefined;
  // #16: reset the session-disable flag so each swap test starts fresh.
  __resetEqDisabledForSession();
  eqState.available = false;
  eqState.reason = '';
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
    expect(mod.playerStore.eqBands).toEqual([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
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
    // progress bar never moves. togglePlay must detect the empty src
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

  it('togglePlay does NOT re-fetch when audio has a valid src even if readyState===0', async () => {
    // Regression guard for the readyState===0 false-positive: a fast pause/
    // resume during loading must resume the existing playback, not restart
    // via playTrack (which would lose currentTime and start from 0).
    initPlayer();
    initPlayerBackend();

    const track = mkTrack('mid-load-track');
    track.Image = 'http://img/';
    playerStore.currentTrack = track;
    playerStore.queue = [track];
    playerStore.currentIndex = 0;
    playerStore.isPlaying = false;
    const audio = (playerStore as any).audio as HTMLAudioElement;
    // Simulate a mid-load state: src is set but readyState is HAVE_NOTHING.
    audio.src = 'http://x/loading.mp3';
    Object.defineProperty(audio, 'readyState', { value: 0, writable: true, configurable: true });

    // Mock the backend to track which path togglePlay takes. If the
    // readyState===0 false-positive regresses, togglePlay will call
    // playTrack → mockInvoke('song_url') → fail, and isPlaying stays false.
    const playUrlCalls: string[] = [];
    const { Html5AudioBackend } = await import('../html5Backend');
    const realPlayUrl = Html5AudioBackend.prototype.playUrl;
    Html5AudioBackend.prototype.playUrl = async function (this: any, url: string) {
      playUrlCalls.push(url);
      return true;
    };

    // Stub song_url to fail so we can detect if playTrack path was taken
    // (a successful playTrack→playUrl would set isPlaying=true via backend,
    // but playTrack path will throw on the song_url invoke stub mismatch).
    let songUrlCalled = false;
    const realInvoke = mockInvoke.getMockImplementation();
    mockInvoke.mockImplementation(async (cmd: string, args?: any) => {
      if (cmd === 'song_url') {
        songUrlCalled = true;
        return JSON.stringify({ status: 200, body: { error: 'blocked by test' } });
      }
      if (realInvoke) return realInvoke(cmd, args);
      return JSON.stringify({ status: 200, body: {} });
    });

    await togglePlay();

    Html5AudioBackend.prototype.playUrl = realPlayUrl;

    // resume path → isPlaying=true, no song_url fetch
    // playTrack path → songUrlCalled=true (we want to assert this is FALSE)
    expect(songUrlCalled, 'togglePlay must not fall through to playTrack when src is valid').toBe(false);
    expect(playUrlCalls, 'playUrl must not be called for resume').toEqual([]);
  });

  it('togglePlay cancels an in-flight play before delayed playUrl can start audio', async () => {
    initPlayer();
    initPlayerBackend();

    const track = mkTrack('slow-load-track');
    track.Image = 'http://img/';
    playerStore.queue = [track];

    const backend = __getActiveBackend() as any;
    const realPlayUrl = backend.playUrl;
    let releasePlayUrl!: () => void;
    let releasedPlayUrl = false;
    const playUrlCanFinish = new Promise<void>((resolve) => {
      releasePlayUrl = () => {
        if (releasedPlayUrl) return;
        releasedPlayUrl = true;
        resolve();
      };
    });
    const playUrlStarted = new Promise<void>((resolve) => {
      backend.playUrl = async (url: string) => {
        (playerStore.audio as HTMLAudioElement).src = url;
        resolve();
        await playUrlCanFinish;
        (playerStore.audio as HTMLAudioElement).dispatchEvent(new Event('play'));
        return true;
      };
    });

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/slow.mp3' } });
    });

    try {
      const playPromise = playTrack(track);
      await playUrlStarted;
      expect(playerStore.isPlaying).toBe(false);

      const togglePromise = togglePlay();
      releasePlayUrl();
      await Promise.allSettled([playPromise, togglePromise]);
      await new Promise((resolve) => setTimeout(resolve, 20));
    } finally {
      releasePlayUrl();
      backend.playUrl = realPlayUrl;
    }

    expect(playerStore.isPlaying, 'canceling during load must not let delayed playUrl flip back to playing').toBe(false);
    expect((playerStore.audio as HTMLAudioElement).paused).toBe(true);
    expect(playerStore.errorMsg).toBe('');
  });

  // ── #16: element-swap after WebAudio wedge (auto-advance silence bug) ──
  // Confirmed root cause: createMediaElementSource is irreversible for the
  // <audio> element's lifetime. Song 1 binds the element + builds the graph.
  // On song 2 auto-advance, the AudioContext may be suspended (no user gesture
  // on auto-advance); resume() rejects → onSuspendedFail fires on an already-
  // bound element → audio is silently trapped. The fix swaps in a fresh
  // <audio> element (un-bound), rebuilds the backend, and re-triggers the
  // current track with EQ disabled for the session.
  it('swaps the <audio> element and re-triggers playback when onSuspendedFail fires on a bound element', async () => {
    // Mock AudioContext: graph builds successfully (createMediaElementSource
    // returns a source node). ctx starts 'running' so song 1's resume() is a
    // no-op. We'll flip it to 'suspended' + rejecting resume for song 2.
    const sourceNode = { connect: vi.fn(() => sourceNode) };
    const mockCtx: any = {
      state: 'running',
      destination: { connect: vi.fn() },
      resume: vi.fn(async () => { mockCtx.state = 'running'; }),
      close: vi.fn(async () => { mockCtx.state = 'closed'; }),
      createMediaElementSource: vi.fn(() => sourceNode),
      createBiquadFilter: vi.fn(() => ({
        connect: vi.fn(() => ({})),
        type: '', frequency: { value: 0 }, Q: { value: 0 }, gain: { value: 0 },
      })),
      createGain: vi.fn(() => ({ connect: vi.fn(() => ({})), gain: { value: 0 } })),
    };
    // Re-stub AudioContext (the file stubs it as undefined at top). The
    // webAudioEq factory reads window.AudioContext lazily on each init().
    vi.stubGlobal('AudioContext', function () { return mockCtx; });

    initPlayer();
    initPlayerBackend();

    // Stub audio.play() to resolve (jsdom's rejects). Track which element
    // instances get a src set so we can detect the swap.
    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const t1 = mkTrack('song1'); t1.Image = 'http://img/';
    const t2 = mkTrack('song2'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];

    // invoke: audio_proxy_url returns a CORS-safe proxy URL (crossOriginSafe
    // true → graph builds); /song/url returns that proxy URL; stats no-op.
    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/1';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/1' } });
    });

    // Song 1: plays fine. Graph builds, element bound, ctx running.
    await playTrack(t1);
    expect(mockCtx.createMediaElementSource, 'song 1 must bind the element').toHaveBeenCalledTimes(1);
    expect(eqState.available, 'EQ available after song 1').toBe(true);
    const audioAfterSong1 = playerStore.audio;

    // Simulate the auto-advance condition: ctx suspends, resume rejects (no
    // user gesture on auto-advance). This is the exact condition that fires
    // onSuspendedFail on song 2.
    mockCtx.state = 'suspended';
    mockCtx.resume = vi.fn(async () => {
      throw new Error('NotAllowedError: no user gesture');
    });

    // Song 2: playUrl → setPreparedSource → initEq (no-op, ctx guard) →
    // audio.play() resolves. In a real browser, audio.play() fires a 'play'
    // event → resumeAudioContext() → resume() rejects → onSuspendedFail on
    // the bound element → SWAP. jsdom's play() is a stub that doesn't fire
    // the event, so dispatch it manually to drive the swap path.
    await playTrack(t2);
    const audioAfterSong2 = playerStore.audio;
    expect(audioAfterSong2).toBe(audioAfterSong1); // no swap yet
    audioAfterSong2!.dispatchEvent(new Event('play'));

    // The swap is triggered from within the 'play' event handler (async).
    // Wait for the swap + re-trigger to settle.
    await vi.waitFor(() => {
      expect(playerStore.audio, 'a fresh <audio> element must have been swapped in').not.toBe(audioAfterSong1);
    });
    // Let the re-trigger finish.
    await new Promise((r) => setTimeout(r, 50));

    // EQ must be disabled for the session and unavailable in the UI.
    expect(eqState.available, 'EQ must be unavailable after the swap').toBe(false);
    expect(eqState.reason, 'degradation reason must be set').toBeTruthy();

    // The fresh element must NOT have a new MediaElementSourceNode bound —
    // createMediaElementSource must not be called again after the swap.
    const cmesCallsAfter = mockCtx.createMediaElementSource.mock.calls.length;
    // Trigger a third "track" to verify the fresh element skips graph building.
    const t3 = mkTrack('song3'); t3.Image = 'http://img/';
    playerStore.queue.push(t3);
    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/2';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/2' } });
    });
    await playTrack(t3);
    expect(mockCtx.createMediaElementSource.mock.calls.length, 'third track must NOT re-bind the fresh element (eqDisabledForSession)').toBe(cmesCallsAfter);

    HTMLAudioElement.prototype.play = realPlay;
  });

  // ── #16: zombie-teardown root cause (the actual InvalidStateError trigger) ──
  // The F12 trace showed InvalidStateError on song 2's createMediaElementSource.
  // Root cause: initPlayer's HMR zombie-teardown fired on EVERY playTrack()
  // call (because __bottlemusic_audio__ is set after the first play), calling
  // webAudioEq.close() → nulling this.ctx → defeating init()'s guard → the
  // next init() re-called createMediaElementSource on the already-bound
  // element → InvalidStateError → silent wedge.
  //
  // The fix: only run the zombie teardown on a genuine HMR reload (when
  // playerStore.audio is null). This test verifies that a second playTrack()
  // does NOT close the WebAudio context (ctx stays set, guard holds, no
  // re-bind of the element).
  it('does NOT close the WebAudio context on a second playTrack (zombie teardown only fires on HMR)', async () => {
    const sourceNode = { connect: vi.fn(() => sourceNode) };
    let closeCalls = 0;
    const mockCtx: any = {
      state: 'running',
      destination: { connect: vi.fn() },
      resume: vi.fn(async () => { mockCtx.state = 'running'; }),
      close: vi.fn(async () => { closeCalls++; mockCtx.state = 'closed'; }),
      createMediaElementSource: vi.fn(() => sourceNode),
      createBiquadFilter: vi.fn(() => ({
        connect: vi.fn(() => ({})),
        type: '', frequency: { value: 0 }, Q: { value: 0 }, gain: { value: 0 },
      })),
      createGain: vi.fn(() => ({ connect: vi.fn(() => ({})), gain: { value: 0 } })),
    };
    vi.stubGlobal('AudioContext', function () { return mockCtx; });

    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const t1 = mkTrack('song1'); t1.Image = 'http://img/';
    const t2 = mkTrack('song2'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/1';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/1' } });
    });

    // Song 1: builds graph, binds element. createMediaElementSource called once.
    await playTrack(t1);
    expect(mockCtx.createMediaElementSource).toHaveBeenCalledTimes(1);
    const cmesAfterSong1 = mockCtx.createMediaElementSource.mock.calls.length;
    expect(closeCalls, 'song 1 must not have closed the context').toBe(0);

    // Song 2: playTrack → initPlayer. WITHOUT the fix, the zombie teardown
    // would fire here (g.__bottlemusic_audio__ is set), close the context,
    // null webAudioEq.ctx, and init() would re-call createMediaElementSource
    // → InvalidStateError. WITH the fix, the teardown does NOT fire (because
    // playerStore.audio is set), so the guard holds and init() is a no-op.
    await playTrack(t2);

    expect(closeCalls, 'song 2 must NOT close the WebAudio context (zombie teardown must not fire)').toBe(0);
    expect(mockCtx.createMediaElementSource.mock.calls.length, 'song 2 must NOT re-call createMediaElementSource (guard holds)').toBe(cmesAfterSong1);
    expect(eqState.available, 'EQ must still be available after song 2').toBe(true);

    HTMLAudioElement.prototype.play = realPlay;
  });
});

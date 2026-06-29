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
  initWebAudioEQ,
  attachWebAudioEqSource,
  disconnectWebAudioEqSource,
  setWebAudioEqVolume,
  __getActiveBackend,
  __getPlaySession,
  __resetWebAudioEqForTests,
  eqState,
} from '../playerStore';
import type { Track } from '../normalizer';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'A', Duration: 100 } as Track;
}

/** Mock AudioContext + captureStream for Phase 2/3 worklet EQ in integration tests. */
function setupWorkletEqMocks() {
  const workletNode = {
    connect: vi.fn((n: unknown) => n),
    disconnect: vi.fn(),
    port: { postMessage: vi.fn() },
    _inputs: [] as Array<{ disconnect: ReturnType<typeof vi.fn> }>,
  };
  const gainNode = {
    connect: vi.fn((n: unknown) => n),
    disconnect: vi.fn(),
    gain: { value: 1 },
  };
  const sourceNodes: Array<{ disconnect: ReturnType<typeof vi.fn>; _connected: boolean }> = [];
  let closeCalls = 0;
  const mockCtx: Record<string, unknown> = {
    state: 'running',
    destination: { connect: vi.fn() },
    resume: vi.fn(async () => {
      mockCtx.state = 'running';
    }),
    close: vi.fn(async () => {
      closeCalls++;
      mockCtx.state = 'closed';
    }),
    audioWorklet: { addModule: vi.fn(async () => {}) },
    createGain: vi.fn(() => gainNode),
    createMediaStreamSource: vi.fn((stream: { _id?: number }) => {
      const node = {
        connect: vi.fn((n: unknown) => {
          if (n === workletNode) {
            workletNode._inputs.push(node);
            node._connected = true;
          }
          return n;
        }),
        disconnect: vi.fn(() => {
          node._connected = false;
          workletNode._inputs = workletNode._inputs.filter((i) => i !== node);
        }),
        _connected: false,
        _stream: stream,
      };
      sourceNodes.push(node);
      return node;
    }),
  };
  vi.stubGlobal('AudioContext', function MockAudioContext() {
    return mockCtx;
  });
  function MockAudioWorkletNode(this: typeof workletNode) {
    return workletNode;
  }
  vi.stubGlobal('AudioWorkletNode', MockAudioWorkletNode);
  vi.stubGlobal('URL', {
    createObjectURL: vi.fn(() => 'blob:eq-test'),
    revokeObjectURL: vi.fn(),
  });
  let streamSeq = 0;
  const allStreams: Array<{ getAudioTracks: () => Array<{ stop: ReturnType<typeof vi.fn>; readyState: string }>; _id: number }> = [];
  const captureStream = vi.fn(function (this: HTMLMediaElement) {
    const tracks = [{ stop: vi.fn(), readyState: 'live' as const }];
    const stream = {
      _id: ++streamSeq,
      getAudioTracks: () => tracks,
    };
    allStreams.push(stream);
    return stream;
  });
  (HTMLMediaElement.prototype as HTMLMediaElement & {
    captureStream: typeof captureStream;
  }).captureStream = captureStream;
  return {
    mockCtx: mockCtx as typeof mockCtx & {
      audioWorklet: { addModule: ReturnType<typeof vi.fn> };
      createMediaStreamSource: ReturnType<typeof vi.fn>;
      resume: ReturnType<typeof vi.fn>;
      close: ReturnType<typeof vi.fn>;
    },
    workletNode,
    gainNode,
    sourceNodes,
    allStreams,
    captureStream,
    getCloseCalls: () => closeCalls,
  };
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
  __resetWebAudioEqForTests();
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

  // ── Phase 3: EQ lifecycle (init at startup, attach post-play) ──
  it('initWebAudioEQ builds graph without audio; attachWebAudioEqSource attaches post-play', async () => {
    const { mockCtx, captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initWebAudioEQ();

    await vi.waitFor(() => expect(mockCtx.audioWorklet.addModule).toHaveBeenCalledTimes(1));

    const audio = playerStore.audio!;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);
    await attachWebAudioEqSource(audio, true);

    expect(captureStream).toHaveBeenCalled();
    expect(mockCtx.createMediaStreamSource).toHaveBeenCalled();
    expect(eqState.available).toBe(true);
  });

  it('disconnectWebAudioEqSource releases stream tracks', async () => {
    const { allStreams } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initWebAudioEQ();
    const audio = playerStore.audio!;
    await attachWebAudioEqSource(audio, true);

    disconnectWebAudioEqSource();

    expect(allStreams[0]!.getAudioTracks()[0]!.stop).toHaveBeenCalled();
    expect(eqState.available).toBe(false);
  });

  it('setWebAudioEqVolume writes gainNode when rerouted', async () => {
    const { gainNode } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initWebAudioEQ();
    await attachWebAudioEqSource(playerStore.audio!, true);

    setWebAudioEqVolume(0.55);
    expect(gainNode.gain.value).toBe(0.55);
  });

  // ── L4: consecutive attachSource does not rebuild worklet graph (spec §7.2) ──
  it('L4: two attachSource calls reuse worklet graph and disconnect old sourceNode', async () => {
    const { mockCtx, workletNode, sourceNodes, captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

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

    await playTrack(t1);
    await vi.waitFor(() => expect(eqState.available).toBe(true));
    expect(mockCtx.audioWorklet.addModule).toHaveBeenCalledTimes(1);
    const firstSource = sourceNodes[0]!;

    await playTrack(t2);
    await vi.waitFor(() => expect(captureStream).toHaveBeenCalledTimes(2));

    expect(mockCtx.audioWorklet.addModule, 'worklet graph built only once').toHaveBeenCalledTimes(1);
    expect(firstSource.disconnect).toHaveBeenCalled();
    expect(workletNode._inputs).toHaveLength(1);
    expect(eqState.available).toBe(true);

    HTMLAudioElement.prototype.play = realPlay;
  });

  // ── L5: crossOriginSafe=false fallback (spec §7.2) ──
  it('L5: proxy failure skips attachSource, degrades EQ, playback still succeeds', async () => {
    const { captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') throw new Error('proxy down');
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://cdn.example/song.mp3' } });
    });

    const track = mkTrack('direct'); track.Image = 'http://img/';
    await playTrack(track);

    expect(captureStream).not.toHaveBeenCalled();
    expect(eqState.available).toBe(false);
    expect(eqState.reason).toBeTruthy();
    expect(playerStore.audio!.src).toContain('song.mp3');
    expect(playerStore.errorMsg).toBe('');

    HTMLAudioElement.prototype.play = realPlay;
  });

  // ── 10-track attachSource regression (spec §7.2 / §10.1) ──
  it('10 consecutive tracks attach distinct streams without graph rebuild or leaks', async () => {
    const { mockCtx, workletNode, sourceNodes, allStreams, captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const tracks = Array.from({ length: 10 }, (_, i) => {
      const t = mkTrack(`song${i}`); t.Image = 'http://img/'; return t;
    });
    playerStore.queue = tracks;

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/x';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/x' } });
    });

    for (let i = 0; i < 10; i++) {
      await playTrack(tracks[i]!);
      await vi.waitFor(() => expect(eqState.available).toBe(true));
    }

    expect(mockCtx.audioWorklet.addModule).toHaveBeenCalledTimes(1);
    expect(captureStream).toHaveBeenCalledTimes(10);
    expect(mockCtx.createMediaStreamSource).toHaveBeenCalledTimes(10);
    const streamIds = new Set(allStreams.map((s) => s._id));
    expect(streamIds.size).toBe(10);
    expect(workletNode._inputs).toHaveLength(1);
    for (let i = 0; i < 9; i++) {
      expect(sourceNodes[i]!.disconnect).toHaveBeenCalled();
    }
    for (const stream of allStreams.slice(0, 9)) {
      expect(stream.getAudioTracks()[0]!.stop).toHaveBeenCalled();
    }

    HTMLAudioElement.prototype.play = realPlay;
  });

  // ── zombie-teardown: second playTrack must not close WebAudio context ──
  it('does NOT close the WebAudio context on a second playTrack (zombie teardown only fires on HMR)', async () => {
    const { mockCtx, getCloseCalls } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

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

    // Song 1: builds worklet graph once, attach via captureStream.
    await playTrack(t1);
    await vi.waitFor(() => expect(eqState.available).toBe(true));
    expect(mockCtx.audioWorklet.addModule).toHaveBeenCalledTimes(1);
    expect(getCloseCalls(), 'song 1 must not have closed the context').toBe(0);

    // Song 2: playTrack → initPlayer. WITHOUT the fix, the zombie teardown
    // would fire here (g.__bottlemusic_audio__ is set), close the context,
    // and break the long-lived worklet graph. WITH the fix, the teardown does
    // NOT fire (because playerStore.audio is set), so the graph stays alive
    // and re-attaches via captureStream (no InvalidStateError).
    await playTrack(t2);

    expect(getCloseCalls(), 'song 2 must NOT close the WebAudio context (zombie teardown must not fire)').toBe(0);
    expect(mockCtx.audioWorklet.addModule, 'worklet graph must be built only once').toHaveBeenCalledTimes(1);
    expect(eqState.available, 'EQ must still be available after song 2').toBe(true);

    HTMLAudioElement.prototype.play = realPlay;
  });
});

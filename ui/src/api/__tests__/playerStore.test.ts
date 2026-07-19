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
  next,
  setQuality,
  togglePlay,
  initWebAudioEQ,
  attachWebAudioEqSource,
  disconnectWebAudioEqSource,
  setWebAudioEqVolume,
  setWebAudioEqEnabled,
  setVolume,
  resumeAudioContext,
  __getActiveBackend,
  __getPlaySession,
  __resetWebAudioEqForTests,
  __resetPlaybackCoordinatorForTests,
  eqState,
  retryEq,
} from '../playerStore';
import type { Track } from '../normalizer';
import { playbackDiagnostics } from '../playbackDiagnostics';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'A', Duration: 100 } as Track;
}

/** Mock AudioContext + captureStream for Phase 2/3 worklet EQ in integration tests. */
function setupWorkletEqMocks() {
  playerStore.eqEnabled = true;
  const workletNode = {
    connect: vi.fn((n: unknown) => n),
    disconnect: vi.fn(),
    port: { postMessage: vi.fn() },
    _inputs: [] as Array<{ disconnect: ReturnType<typeof vi.fn> }>,
  };
  const gainNode = {
    connect: vi.fn((n: unknown) => n),
    disconnect: vi.fn(),
    gain: {
      value: 1,
      cancelScheduledValues: vi.fn(function (this: { value: number }) { return this; }),
      setValueAtTime: vi.fn(function (this: { value: number }, v: number) {
        this.value = v;
        return this;
      }),
      linearRampToValueAtTime: vi.fn(function (this: { value: number }, v: number) {
        this.value = v;
        return this;
      }),
    },
  };
  const sourceNodes: Array<{ disconnect: ReturnType<typeof vi.fn>; _connected: boolean }> = [];
  let closeCalls = 0;
  const mockCtx: Record<string, unknown> = {
    state: 'running',
    currentTime: 0,
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
  playerStore.queueMode = 'normal';
  playerStore.currentTrack = null;
  playerStore.isPlaying = false;
  playerStore.isLoading = false;
  playerStore.currentTime = 0;
  playerStore.duration = 0;
  playerStore.errorMsg = '';
  playerStore.playbackPhase = 'idle';
  (playerStore as any).audio = null;
  // Clear the zombie-audio sentinel so initPlayer() doesn't run its teardown
  // path (which nulls activeBackend) and skip re-creating the backend.
  (window as any).__bottlemusic_audio__ = undefined;
  (window as any).__bottlemusic_player_cleanup__ = undefined;
  __resetWebAudioEqForTests();
  __resetPlaybackCoordinatorForTests();
  eqState.available = false;
  eqState.reason = '';
  eqState.retryFailCount = 0;
  eqState.retryDisabled = false;
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
    // playAll is async via coordinator — must await before snapshotting index.
    await playAll([mkTrack('good')], 0);
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

  it('does not commit a requested quality when resolving that quality fails', async () => {
    const current = mkTrack('quality-current');
    current.Image = 'http://img/';
    playerStore.currentTrack = current;
    playerStore.queue = [current];
    playerStore.currentIndex = 0;
    playerStore.quality = '128';
    localStorage.setItem('player_quality', '128');
    initPlayer();
    initPlayerBackend();
    mockInvoke.mockResolvedValue(JSON.stringify({
      status: 200,
      headers: {},
      body: { status: 0, error: 'quality unavailable' },
    }));

    await setQuality('320');

    expect(playerStore.quality).toBe('128');
    expect(localStorage.getItem('player_quality')).toBe('128');
  });

  it('clears playing when the HTML5 backend emits a media error', () => {
    initPlayer();
    initPlayerBackend();
    playerStore.isPlaying = true;
    playerStore.playbackPhase = 'playing';

    (playerStore.audio as HTMLAudioElement).dispatchEvent(new Event('error'));

    expect(playerStore.isPlaying).toBe(false);
    expect(playerStore.playbackPhase).toBe('error');
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

  it.each([
    ['abc', 0.7],
    ['', 0.7],
    ['5', 1],
    ['-1', 0],
  ] as const)(
    'loadNumber clamps or falls back for player_volume=%j → %s',
    async (raw, expected) => {
      vi.resetModules();
      localStorage.clear();
      localStorage.setItem('player_volume', raw);
      const mod = await import('../playerStore');
      expect(Number.isFinite(mod.playerStore.volume)).toBe(true);
      expect(mod.playerStore.volume).toBe(expected);
      expect(mod.playerStore.volume).toBeGreaterThanOrEqual(0);
      expect(mod.playerStore.volume).toBeLessThanOrEqual(1);
    },
  );

  it('personal FM appends fresh recommendations instead of wrapping at the queue tail', async () => {
    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const t1 = mkTrack('fm-1'); t1.Image = 'http://img/';
    const t2 = mkTrack('fm-2'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];
    playerStore.currentIndex = 1;
    playerStore.currentTrack = t2;
    playerStore.queueMode = 'personalFm';

    mockInvoke.mockImplementation(async (cmd: string, args?: any) => {
      if (cmd === 'stats_record_play') return '';
      if (cmd === 'native_request' && args?.path === '/personal/fm') {
        return JSON.stringify({
          status: 200,
          headers: {},
          body: {
            status: 1,
            data: {
              song_list: [
                { hash: 'fm-3', songname: 'Fresh FM', singername: 'Reco', duration: 210, album_audio_id: '3003', img: 'http://img/' },
              ],
            },
          },
        });
      }
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fm.mp3' } });
    });

    await next();

    HTMLAudioElement.prototype.play = realPlay;

    expect(playerStore.queue.map((track) => track.FileHash)).toEqual(['fm-1', 'fm-2', 'fm-3']);
    expect(playerStore.currentIndex).toBe(2);
    expect(playerStore.currentTrack?.FileHash).toBe('fm-3');
  });

  it('records fm_fetch diagnostics when personal FM recommendations are appended', async () => {
    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const t1 = mkTrack('fm-1'); t1.Image = 'http://img/';
    const t2 = mkTrack('fm-2'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];
    playerStore.currentIndex = 1;
    playerStore.currentTrack = t2;
    playerStore.queueMode = 'personalFm';

    mockInvoke.mockImplementation(async (cmd: string, args?: any) => {
      if (cmd === 'stats_record_play') return '';
      if (cmd === 'native_request' && args?.path === '/personal/fm') {
        return JSON.stringify({
          status: 200,
          headers: {},
          body: {
            status: 1,
            data: {
              song_list: [
                { hash: 'fm-3', songname: 'Fresh FM', singername: 'Reco', duration: 210, album_audio_id: '3003', img: 'http://img/' },
              ],
            },
          },
        });
      }
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fm.mp3' } });
    });

    playbackDiagnostics.reset();
    await next();
    HTMLAudioElement.prototype.play = realPlay;

    const fmDiags = playbackDiagnostics.getEvents().filter((e) => e.kind === 'fm_fetch');
    expect(fmDiags.length).toBeGreaterThan(0);
    expect(fmDiags).toContainEqual(
      expect.objectContaining({ kind: 'fm_fetch', phase: 'ok' }),
    );
  });

  it('personal FM retries semantic recommendation failures before giving up at the queue tail', async () => {
    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const t1 = mkTrack('fm-1'); t1.Image = 'http://img/';
    const t2 = mkTrack('fm-2'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];
    playerStore.currentIndex = 1;
    playerStore.currentTrack = t2;
    playerStore.queueMode = 'personalFm';

    let personalFmCalls = 0;
    mockInvoke.mockImplementation(async (cmd: string, args?: any) => {
      if (cmd === 'stats_record_play') return '';
      if (cmd === 'native_request' && args?.path === '/personal/fm') {
        personalFmCalls++;
        if (personalFmCalls === 1) {
          return JSON.stringify({
            status: 200,
            headers: {},
            body: { status: 0, error: 'WinHttpSendRequest/WinHttpReceiveResponse failed with Win32 error 12175' },
          });
        }
        return JSON.stringify({
          status: 200,
          headers: {},
          body: {
            status: 1,
            data: {
              song_list: [
                { hash: 'fm-3', songname: 'Fresh FM', singername: 'Reco', duration: 210, album_audio_id: '3003', img: 'http://img/' },
              ],
            },
          },
        });
      }
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fm.mp3' } });
    });

    await next();

    HTMLAudioElement.prototype.play = realPlay;

    expect(personalFmCalls).toBe(2);
    expect(playerStore.currentIndex).toBe(2);
    expect(playerStore.currentTrack?.FileHash).toBe('fm-3');
  });

  it('ignores duplicate ended events while personal FM tail advance is still switching to the first fresh track', async () => {
    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const lastOld = mkTrack('old-last'); lastOld.Image = 'http://img/';
    playerStore.queue = [lastOld];
    playerStore.currentIndex = 0;
    playerStore.currentTrack = lastOld;
    playerStore.queueMode = 'personalFm';

    let resolveFirstSongUrl!: (value: string) => void;
    const firstSongUrl = new Promise<string>((resolve) => { resolveFirstSongUrl = resolve; });

    mockInvoke.mockImplementation(async (cmd: string, args?: any) => {
      if (cmd === 'stats_record_play') return '';
      if (cmd === 'native_request' && args?.path === '/personal/fm') {
        return JSON.stringify({
          status: 200,
          headers: {},
          body: {
            status: 1,
            data: {
              song_list: [
                { hash: 'fresh-1', songname: '风吹麦浪', singername: 'A', duration: 210, album_audio_id: '3001', img: 'http://img/' },
                { hash: 'fresh-2', songname: '凉凉', singername: 'B', duration: 220, album_audio_id: '3002', img: 'http://img/' },
              ],
            },
          },
        });
      }
      if (cmd === 'native_request' && args?.path === '/song/url') {
        if (args?.queryJson?.includes('fresh-1')) return firstSongUrl;
        return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fresh-2.mp3' } });
      }
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1 } });
    });

    const audio = playerStore.audio as HTMLAudioElement;
    audio.dispatchEvent(new Event('ended'));

    await vi.waitFor(() => {
      expect(playerStore.currentTrack?.FileHash).toBe('fresh-1');
    });

    audio.dispatchEvent(new Event('ended'));
    resolveFirstSongUrl(JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/fresh-1.mp3' } }));

    await vi.waitFor(() => {
      expect(playerStore.isLoading).toBe(false);
    });

    HTMLAudioElement.prototype.play = realPlay;

    expect(playerStore.currentTrack?.FileHash).toBe('fresh-1');
    expect(playerStore.currentIndex).toBe(1);
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

  // ── P1.1: stale post-play attach must not run initEq / captureStream ──
  it('P1.1: late play() resolve after track switch does not attach EQ for stale transition', async () => {
    const { captureStream, allStreams } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    let releasePlay!: () => void;
    const playGate = new Promise<void>((resolve) => {
      releasePlay = resolve;
    });
    let playCallCount = 0;
    HTMLAudioElement.prototype.play = vi.fn(async function (this: HTMLAudioElement) {
      playCallCount += 1;
      if (playCallCount === 1) {
        await playGate;
      }
    });

    const t1 = mkTrack('song-a'); t1.Image = 'http://img/';
    const t2 = mkTrack('song-b'); t2.Image = 'http://img/';
    playerStore.queue = [t1, t2];

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/x';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/x' } });
    });

    try {
      const playA = playTrack(t1);
      await vi.waitFor(() => expect(HTMLAudioElement.prototype.play).toHaveBeenCalledTimes(1));

      await playTrack(t2);
      await vi.waitFor(() => expect(captureStream).toHaveBeenCalledTimes(1));

      const bStream = allStreams[allStreams.length - 1]!;
      const bTrack = bStream.getAudioTracks()[0]!;

      releasePlay();
      await playA;

      expect(captureStream, 'stale transition A must not attach after B is live').toHaveBeenCalledTimes(1);
      expect(bTrack.stop, 'B stream tracks must stay live').not.toHaveBeenCalled();
    } finally {
      releasePlay();
      HTMLAudioElement.prototype.play = realPlay;
    }
  });

  // ── P1.2: initEq already fired, cancel disconnects capture stream ──
  it('P1.2: cancel after initEq fires disconnects capture stream tracks', async () => {
    const { captureStream, allStreams } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();

    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const backend = __getActiveBackend()!;
    const realPlayUrl = backend.playUrl.bind(backend);
    let releasePlayUrl!: () => void;
    const playUrlGate = new Promise<void>((resolve) => {
      releasePlayUrl = resolve;
    });
    backend.playUrl = async (url: string) => {
      const ok = await realPlayUrl(url);
      await playUrlGate;
      return ok;
    };

    const track = mkTrack('slow-load'); track.Image = 'http://img/';
    playerStore.queue = [track];

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/x';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/x' } });
    });

    try {
      const playPromise = playTrack(track);
      await vi.waitFor(() => expect(captureStream).toHaveBeenCalledTimes(1));
      const aTrack = allStreams[0]!.getAudioTracks()[0]!;

      await togglePlay();
      expect(aTrack.stop).toHaveBeenCalled();
      expect(eqState.available).toBe(false);

      releasePlayUrl();
      await playPromise;
    } finally {
      releasePlayUrl();
      backend.playUrl = realPlayUrl;
    }
  });

  // ── P2.3: volume watch routes to EQ gain and backend.setVolume once ──
  it('P2.3: setVolume updates gainNode and backend.setVolume exactly once each', async () => {
    const { gainNode } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);
    await attachWebAudioEqSource(playerStore.audio!, true);

    const backend = __getActiveBackend()!;
    const setVolumeSpy = vi.spyOn(backend, 'setVolume');

    await setVolume(0.42);

    expect(gainNode.gain.value).toBe(0.42);
    expect(setVolumeSpy).toHaveBeenCalledTimes(1);
    expect(setVolumeSpy).toHaveBeenCalledWith(0.42);
    expect(playerStore.volume).toBe(0.42);
  });

  it('turning EQ OFF during playback restores direct HTML5 output and releases the captured stream', async () => {
    const { allStreams } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();
    playerStore.volume = 0.57;

    const audio = playerStore.audio!;
    await attachWebAudioEqSource(audio, true);
    const streamTrack = allStreams[0]!.getAudioTracks()[0]!;

    setWebAudioEqEnabled(false);

    expect(streamTrack.stop).toHaveBeenCalled();
    expect(eqState.available).toBe(false);
    expect(audio.volume).toBeCloseTo(0.57);
  });

  it('turning EQ back ON during playback reattaches the same safe audio source', async () => {
    const { allStreams, captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();
    const audio = playerStore.audio!;
    audio.src = 'http://127.0.0.1:17631/audio/eq-live';
    await attachWebAudioEqSource(audio, true);
    const firstTrack = allStreams[0]!.getAudioTracks()[0]!;

    setWebAudioEqEnabled(false);
    expect(firstTrack.stop).toHaveBeenCalled();
    expect(eqState.available).toBe(false);

    setWebAudioEqEnabled(true);

    await vi.waitFor(() => expect(captureStream).toHaveBeenCalledTimes(2));
    expect(eqState.available).toBe(true);
    expect(allStreams[1]!.getAudioTracks()[0]!.stop).not.toHaveBeenCalled();
  });

  it('does not reuse a stale safe EQ source marker after switching to a direct fallback while EQ is off', async () => {
    const { captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initPlayerBackend();
    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    const safeTrack = mkTrack('safe-eq'); safeTrack.Image = 'http://img/';
    const directTrack = mkTrack('direct-eq'); directTrack.Image = 'http://img/';
    let proxyShouldFail = false;
    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') {
        if (proxyShouldFail) throw new Error('proxy down');
        return 'http://127.0.0.1:17631/audio/safe-eq';
      }
      if (cmd === 'stats_record_play') return '';
      const url = proxyShouldFail
        ? 'https://fs.wbpz.kugou.com/direct.mp3'
        : 'https://fs.wbpz.kugou.com/safe.mp3';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url } });
    });

    try {
      await playTrack(safeTrack);
      await vi.waitFor(() => expect(captureStream).toHaveBeenCalledTimes(1));

      playerStore.eqEnabled = false;
      setWebAudioEqEnabled(false);
      proxyShouldFail = true;
      await playTrack(directTrack);

      playerStore.eqEnabled = true;
      setWebAudioEqEnabled(true);
      await new Promise((resolve) => setTimeout(resolve, 20));

      expect(captureStream, 'direct fallback must not be rerouted from a stale safe marker')
        .toHaveBeenCalledTimes(1);
      expect(eqState.available).toBe(false);
    } finally {
      HTMLAudioElement.prototype.play = realPlay;
    }
  });

  it('does not reroute a direct source when a previous safe EQ marker is stale', async () => {
    const { captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initWebAudioEQ();
    const audio = playerStore.audio!;
    audio.src = 'http://127.0.0.1:17631/audio/safe-before-direct';
    await attachWebAudioEqSource(audio, true);
    expect(captureStream).toHaveBeenCalledTimes(1);

    setWebAudioEqEnabled(false);
    audio.src = 'https://fs.wbpz.kugou.com/direct-after-safe.mp3';

    setWebAudioEqEnabled(true);
    await new Promise((resolve) => setTimeout(resolve, 20));

    expect(captureStream, 'a stale safe marker must not reroute a direct CDN source')
      .toHaveBeenCalledTimes(1);
    expect(eqState.available).toBe(false);
  });

  // ── P2.4: resumeAudioContext failure enters degradation when rerouted ──
  it('P2.4: resumeAudioContext reject while rerouted enters degradation', async () => {
    const { mockCtx, sourceNodes } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    initPlayer();
    initWebAudioEQ();
    await vi.waitFor(() => expect(mockCtx.audioWorklet.addModule).toHaveBeenCalled());

    const audio = playerStore.audio!;
    playerStore.volume = 0.55;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);
    await attachWebAudioEqSource(audio, true);

    mockCtx.state = 'suspended';
    mockCtx.resume = vi.fn(async () => {
      throw new Error('NotAllowedError');
    });

    resumeAudioContext();
    await vi.waitFor(() => expect(eqState.available).toBe(false));

    expect(audio.volume).toBe(0.55);
    expect(sourceNodes[0]!.disconnect).toHaveBeenCalled();
    expect(eqState.reason).toContain('重试');
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

  it('keeps EQ OFF on the direct CDN path without registering the local audio proxy', async () => {
    const { captureStream } = setupWorkletEqMocks();
    __resetWebAudioEqForTests();

    playerStore.eqEnabled = false;
    playerStore.volume = 0.64;
    initPlayer();
    initPlayerBackend();

    const realPlay = HTMLAudioElement.prototype.play;
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') throw new Error('EQ off must not register proxy routes');
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'https://fs.wbpz.kugou.com/song.mp3' } });
    });

    const track = mkTrack('eq-off-direct'); track.Image = 'http://img/';
    await playTrack(track);

    expect(captureStream, 'EQ OFF should not reroute audio through AudioWorklet').not.toHaveBeenCalled();
    expect(mockInvoke.mock.calls.some((c) => c[0] === 'audio_proxy_url')).toBe(false);
    expect(eqState.available).toBe(false);
    expect(playerStore.audio!.volume).toBeCloseTo(0.64);
    expect(playerStore.audio!.src).toContain('https://fs.wbpz.kugou.com/song.mp3');

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

  it('reuses the existing audio element across HMR without unloading the current source', () => {
    const oldAudio = document.createElement('audio') as HTMLAudioElement;
    oldAudio.src = 'http://127.0.0.1:17631/audio/hmr-live';
    Object.defineProperty(oldAudio, 'duration', { value: 227, configurable: true });
    Object.defineProperty(oldAudio, 'currentTime', { value: 42, writable: true, configurable: true });
    const pauseSpy = vi.spyOn(oldAudio, 'pause').mockImplementation(() => {});
    const loadSpy = vi.spyOn(oldAudio, 'load').mockImplementation(() => {});
    const removeAttrSpy = vi.spyOn(oldAudio, 'removeAttribute');

    (window as any).__bottlemusic_audio__ = oldAudio;
    (playerStore as any).audio = null;
    playerStore.currentTime = 0;
    playerStore.duration = 0;

    initPlayer();

    expect(playerStore.audio).toBe(oldAudio);
    expect(playerStore.audio!.src).toContain('/audio/hmr-live');
    expect(playerStore.currentTime).toBe(42);
    expect(playerStore.duration).toBe(227);
    expect(pauseSpy).not.toHaveBeenCalled();
    expect(loadSpy).not.toHaveBeenCalled();
    expect(removeAttrSpy).not.toHaveBeenCalledWith('src');
  });

  it('HMR module cleanup detaches coordinator without pause or clearing shared audio src', async () => {
    // Full path: old module cleanup must not run dispose barrier on shared <audio>.
    initPlayer();
    initPlayerBackend();
    HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/x';
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({
        status: 200,
        headers: {},
        body: { status: 1, url: 'http://127.0.0.1:17631/audio/hmr-keep' },
      });
    });

    const track = mkTrack('hmr-keep');
    track.Image = 'http://img/';
    await playTrack(track);

    const audio = playerStore.audio!;
    expect(audio.src).toBeTruthy();
    const srcBefore = audio.src;
    const pauseSpy = vi.spyOn(audio, 'pause');

    const cleanup = (window as any).__bottlemusic_player_cleanup__ as (() => void) | undefined;
    expect(cleanup, 'HMR cleanup must be published').toEqual(expect.any(Function));
    cleanup!();

    // Allow any async detach/dispose microtasks to settle.
    await Promise.resolve();
    await Promise.resolve();
    await new Promise((r) => setTimeout(r, 30));

    expect(pauseSpy, 'HMR must not pause shared audio').not.toHaveBeenCalled();
    expect(audio.src, 'HMR must not clear shared audio src').toBe(srcBefore);
    expect(audio.src).not.toBe('');
    expect(audio.src).not.toMatch(/about:blank|^$/);

    // New module instance reuses the same element without unload.
    (playerStore as any).audio = null;
    (window as any).__bottlemusic_audio__ = audio;
    initPlayer();
    expect(playerStore.audio).toBe(audio);
    expect(playerStore.audio!.src).toBe(srcBefore);
  });

  it('syncs playing state from a reused audio element after HMR', () => {
    const oldAudio = document.createElement('audio') as HTMLAudioElement;
    oldAudio.src = 'http://127.0.0.1:17631/audio/hmr-playing';
    Object.defineProperty(oldAudio, 'paused', { value: false, configurable: true });
    Object.defineProperty(oldAudio, 'ended', { value: false, configurable: true });

    (window as any).__bottlemusic_audio__ = oldAudio;
    (playerStore as any).audio = null;
    playerStore.isPlaying = false;

    initPlayer();

    expect(playerStore.audio).toBe(oldAudio);
    expect(playerStore.isPlaying).toBe(true);
  });

  // ── Phase 4: retryEq failure count (spec §6.3) ──
  describe('retryEq', () => {
    let eqMocks: ReturnType<typeof setupWorkletEqMocks>;

    beforeEach(() => {
      eqMocks = setupWorkletEqMocks();
      __resetWebAudioEqForTests();
      initPlayer();
      initWebAudioEQ();
    });

    it('success: resume resolves → recoverFromDegradation → eqState.available=true, retryFailCount=0', async () => {
      const audio = playerStore.audio!;
      await attachWebAudioEqSource(audio, true);

      eqMocks.mockCtx.state = 'suspended';
      eqState.available = false;
      eqState.retryFailCount = 2;

      await retryEq();

      expect(eqState.available).toBe(true);
      expect(eqState.reason).toBe('');
      expect(eqState.retryFailCount).toBe(0);
      expect(eqState.retryDisabled).toBe(false);
    });

    it('failure once: retryFailCount=1, button still enabled', async () => {
      eqMocks.mockCtx.state = 'suspended';
      eqMocks.mockCtx.resume = vi.fn(async () => {
        throw new Error('NotAllowedError');
      });

      await retryEq();

      expect(eqState.retryFailCount).toBe(1);
      expect(eqState.retryDisabled).toBe(false);
    });

    it('failure 3 times: retryFailCount=3, retryDisabled=true', async () => {
      eqMocks.mockCtx.state = 'suspended';
      eqMocks.mockCtx.resume = vi.fn(async () => {
        throw new Error('NotAllowedError');
      });

      await retryEq();
      await retryEq();
      await retryEq();

      expect(eqState.retryFailCount).toBe(3);
      expect(eqState.retryDisabled).toBe(true);
    });

    it('track switch resets retryFailCount and retryDisabled (spec §6.3)', async () => {
      eqState.retryFailCount = 3;
      eqState.retryDisabled = true;

      const realPlay = HTMLAudioElement.prototype.play;
      HTMLAudioElement.prototype.play = vi.fn().mockResolvedValue(undefined);

      mockInvoke.mockImplementation(async (cmd: string) => {
        if (cmd === 'audio_proxy_url') return 'http://127.0.0.1:17631/audio/1';
        if (cmd === 'stats_record_play') return '';
        return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://127.0.0.1:17631/audio/1' } });
      });

      const t1 = mkTrack('reset1'); t1.Image = 'http://img/';
      const t2 = mkTrack('reset2'); t2.Image = 'http://img/';
      playerStore.queue = [t1, t2];

      await playTrack(t1);

      expect(eqState.retryFailCount).toBe(0);
      expect(eqState.retryDisabled).toBe(false);

      HTMLAudioElement.prototype.play = realPlay;
    });
  });
});

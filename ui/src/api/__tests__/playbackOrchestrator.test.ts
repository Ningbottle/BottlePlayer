import { describe, expect, it, vi } from 'vitest';
import type { Track } from '../normalizer';
import {
  PlaybackOrchestrator,
  type PlaybackStateSlice,
} from '../playbackOrchestrator';
import type { ResolveTrackResult } from '../../playback/types';
import { Html5AudioBackend } from '../html5Backend';
import type { DiagEvent } from '../playbackDiagnostics';

function mkTrack(hash: string, name = hash): Track {
  return {
    FileHash: hash,
    SongName: name,
    SingerName: 'Artist',
    Duration: 100,
    Image: 'http://img/',
  } as Track;
}

function makeState(): PlaybackStateSlice {
  return {
    currentTrack: null,
    currentIndex: -1,
    queue: [],
    currentTime: 0,
    duration: 0,
    isPlaying: false,
    isLoading: false,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    quality: '128',
    availableQualities: [],
    playbackPhase: 'idle',
  };
}

function makeHarness(options: { calls?: string[] } = {}) {
  const calls = options.calls ?? [];
  const state = makeState();
  const backend = {
    playUrl: vi.fn(async (url: string) => {
      calls.push(`playUrl:${url}`);
      return true;
    }),
    stop: vi.fn(async () => {
      calls.push('stop');
    }),
    switchUrl: vi.fn(),
    hasSource: vi.fn(() => true),
    resume: vi.fn(async () => {
      calls.push('resume');
    }),
    pause: vi.fn(),
    seek: vi.fn(async (seconds: number) => {
      calls.push(`seek:${seconds}`);
    }),
  };
  const playSession = {
    skip: vi.fn(() => {
      calls.push('skip');
    }),
    intend: vi.fn((track: Track) => {
      calls.push(`intend:${track.FileHash}`);
    }),
  };
  const resolveTrack = vi.fn<(
    track: Track,
    quality: string,
  ) => Promise<ResolveTrackResult>>(async (track) => {
    calls.push(`resolve:${track.FileHash}`);
    return {
      status: 1,
      url: 'http://x/song.mp3',
      data: { available_qualities: [] },
    };
  });
  const fetchCover = vi.fn();
  const uploadPlayHistory = vi.fn((track: Track) => {
    calls.push(`upload:${track.FileHash}`);
  });
  const recordRecentPlayed = vi.fn((track: Track) => {
    calls.push(`record:${track.FileHash}`);
  });
  const recordDiagnostic = vi.fn((e: Omit<DiagEvent, 'ts'>) => {
    calls.push(`diag:${e.kind}:${e.phase}`);
  });
  const saveQueue = vi.fn(() => {
    calls.push('saveQueue');
  });

  const orchestrator = new PlaybackOrchestrator({
    backend: () => backend,
    playSession,
    resolveTrack,
    fetchCover,
    uploadPlayHistory,
    recordRecentPlayed,
    recordDiagnostic,
    getState: () => state,
    patchState: (patch) => Object.assign(state, patch),
    saveQueue,
  });

  return {
    backend,
    calls,
    fetchCover,
    orchestrator,
    playSession,
    recordDiagnostic,
    recordRecentPlayed,
    resolveTrack,
    saveQueue,
    state,
    uploadPlayHistory,
  };
}

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function resolvedTrack(url: string): ResolveTrackResult {
  return {
    status: 1,
    url,
    data: { available_qualities: [] },
  };
}

describe('PlaybackOrchestrator', () => {
  it('records recent-played after a successful switchTrack (not on failure or superseded)', async () => {
    const h = makeHarness();

    // Success: recordRecentPlayed fires with the played track.
    await h.orchestrator.switchTrack(mkTrack('ok'));
    expect(h.recordRecentPlayed).toHaveBeenCalledWith(
      expect.objectContaining({ FileHash: 'ok' }),
    );

    // Failure (playUrl returns false): does NOT fire.
    h.recordRecentPlayed.mockClear();
    h.backend.playUrl.mockResolvedValue(false);
    await h.orchestrator.switchTrack(mkTrack('bad'));
    expect(h.recordRecentPlayed).not.toHaveBeenCalled();
  });

  it('records track_switch start + url_resolve start/ok diagnostics on a successful switchTrack', async () => {
    const h = makeHarness();
    await h.orchestrator.switchTrack(mkTrack('h1'));

    const diags = h.recordDiagnostic.mock.calls.map((c) => c[0]);
    expect(diags).toContainEqual(
      expect.objectContaining({ kind: 'track_switch', phase: 'start', trackKey: 'h1' }),
    );
    expect(diags).toContainEqual(
      expect.objectContaining({ kind: 'url_resolve', phase: 'start' }),
    );
    expect(diags).toContainEqual(
      expect.objectContaining({ kind: 'url_resolve', phase: 'ok' }),
    );
  });

  it('records url_resolve fail diagnostic when resolve throws', async () => {
    const h = makeHarness();
    h.resolveTrack.mockRejectedValue(new Error('vip'));
    await h.orchestrator.switchTrack(mkTrack('bad'));
    const diags = h.recordDiagnostic.mock.calls.map((c) => c[0]);
    expect(diags).toContainEqual(
      expect.objectContaining({ kind: 'url_resolve', phase: 'fail' }),
    );
  });

  it('switchTrack orders skip, Resolve, intend, playUrl, saveQueue', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });

    const result = await h.orchestrator.switchTrack(mkTrack('h1'));

    expect(result).toEqual({ status: 'played' });
    expect(calls).toEqual([
      'skip',
      'stop',
      'diag:track_switch:start',
      'diag:url_resolve:start',
      'resolve:h1',
      'diag:url_resolve:ok',
      'intend:h1',
      'playUrl:http://x/song.mp3',
      'saveQueue',
      'upload:h1',
      'record:h1',
    ]);
    expect(h.state.currentTrack?.FileHash).toBe('h1');
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.errorMsg).toBe('');
  });

  it('switchTrack fetches missing cover without blocking playback', async () => {
    const h = makeHarness();
    h.fetchCover.mockResolvedValue('http://img/new.jpg');
    const track = mkTrack('needs-cover');
    delete track.Image;

    const result = await h.orchestrator.switchTrack(track);

    expect(result).toEqual({ status: 'played' });
    expect(h.fetchCover).toHaveBeenCalledWith('needs-cover');
    await vi.waitFor(() => {
      expect(h.state.currentTrack?.Image).toBe('http://img/new.jpg');
    });
    expect(h.state.queue[0]?.Image).toBe('http://img/new.jpg');
  });

  it('rolls back current pointer when Resolve fails without removing queued track', async () => {
    const h = makeHarness();
    const good = mkTrack('good');
    h.state.queue = [good];
    h.state.currentIndex = 0;
    h.state.currentTrack = good;
    h.resolveTrack.mockRejectedValue(new Error('vip'));

    const result = await h.orchestrator.switchTrack(mkTrack('bad'));

    expect(result).toEqual({ status: 'failed', message: 'vip' });
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.currentTrack?.FileHash).toBe('good');
    expect(h.state.queue.some((t) => t.FileHash === 'bad')).toBe(true);
    expect(h.saveQueue).toHaveBeenCalled();
    expect(h.backend.playUrl).not.toHaveBeenCalled();
  });

  it('cleans up pending session and rolls back when playUrl fails', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });
    const good = mkTrack('good');
    h.state.queue = [good];
    h.state.currentIndex = 0;
    h.state.currentTrack = good;
    h.backend.playUrl.mockImplementation(async (url: string) => {
      calls.push(`playUrl:${url}`);
      return false;
    });

    const result = await h.orchestrator.switchTrack(mkTrack('bad'));

    expect(result).toEqual({ status: 'failed', message: '播放失败' });
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.currentTrack?.FileHash).toBe('good');
    expect(calls).toEqual([
      'skip',
      'stop',
      'diag:track_switch:start',
      'diag:url_resolve:start',
      'resolve:bad',
      'diag:url_resolve:ok',
      'intend:bad',
      'playUrl:http://x/song.mp3',
      'skip',
      'saveQueue',
    ]);
  });

  it('turns a current playUrl rejection into a failed result and clears loading', async () => {
    const h = makeHarness();
    h.backend.playUrl.mockRejectedValueOnce(new Error('proxy down'));

    await expect(h.orchestrator.switchTrack(mkTrack('a'))).resolves.toEqual({
      status: 'failed',
      message: 'proxy down',
    });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.errorMsg).toBe('proxy down');
  });

  it('reports a late playUrl rejection as superseded without contaminating B', async () => {
    const h = makeHarness();
    const rejectA = deferred<boolean>();
    h.backend.playUrl.mockImplementationOnce(() => rejectA.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => expect(h.backend.playUrl).toHaveBeenCalledTimes(1));
    await expect(h.orchestrator.switchTrack(mkTrack('b'))).resolves.toEqual({ status: 'played' });

    rejectA.reject(new Error('late proxy failure'));
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.errorMsg).toBe('');
    expect(h.uploadPlayHistory).not.toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'a' }));
    expect(h.recordDiagnostic).toHaveBeenCalledWith(expect.objectContaining({
      kind: 'track_switch', phase: 'noop', trackKey: 'a',
    }));
  });

  it('turns a current switchUrl rejection into a failed quality result', async () => {
    const h = makeHarness();
    const current = mkTrack('a');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.state.isPlaying = true;
    h.state.isLoading = true;
    h.state.availableQualities = [{ quality: '320', url: 'http://x/320.mp3' }];
    h.backend.switchUrl.mockRejectedValueOnce(new Error('quality proxy down'));

    await expect(h.orchestrator.switchQuality('320')).resolves.toEqual({
      status: 'failed', message: 'quality proxy down',
    });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
    expect(h.state.errorMsg).toBe('quality proxy down');
  });

  it('keeps the current playback phase when quality resolution fails before switching source', async () => {
    const h = makeHarness();
    const current = mkTrack('a');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.state.isPlaying = true;
    h.state.playbackPhase = 'playing';
    h.resolveTrack.mockRejectedValueOnce(new Error('quality unavailable'));

    await expect(h.orchestrator.switchQuality('320')).resolves.toEqual({
      status: 'failed', message: 'quality unavailable',
    });

    expect(h.backend.switchUrl).not.toHaveBeenCalled();
    expect(h.state.isPlaying).toBe(true);
    expect(h.state.playbackPhase).toBe('playing');
  });

  it('reports a late switchUrl rejection as superseded without contaminating B', async () => {
    const h = makeHarness();
    const current = mkTrack('a');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.state.availableQualities = [{ quality: '320', url: 'http://x/320.mp3' }];
    const rejectA = deferred<boolean>();
    h.backend.switchUrl.mockImplementationOnce(() => rejectA.promise);

    const quality = h.orchestrator.switchQuality('320');
    await vi.waitFor(() => expect(h.backend.switchUrl).toHaveBeenCalledTimes(1));
    await expect(h.orchestrator.switchTrack(mkTrack('b'))).resolves.toEqual({ status: 'played' });

    rejectA.reject(new Error('late quality failure'));
    await expect(quality).resolves.toEqual({ status: 'superseded' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.errorMsg).toBe('');
    expect(h.recordDiagnostic).toHaveBeenCalledWith(expect.objectContaining({
      kind: 'track_switch', phase: 'noop', trackKey: 'a',
    }));
  });

  it('pure invalidatePlaybackIntent supersedes a captured switch without stopping or committing it', async () => {
    const h = makeHarness();
    const resolveA = deferred<ResolveTrackResult>();
    h.resolveTrack.mockImplementationOnce(() => resolveA.promise);
    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => expect(h.resolveTrack).toHaveBeenCalledTimes(1));
    const stopsBefore = h.backend.stop.mock.calls.length;

    h.orchestrator.invalidatePlaybackIntent();
    resolveA.resolve(resolvedTrack('http://x/a.mp3'));
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.backend.stop).toHaveBeenCalledTimes(stopsBefore);
    expect(h.backend.playUrl).not.toHaveBeenCalled();
    expect(h.uploadPlayHistory).not.toHaveBeenCalled();
  });

  it('stopInvalidatedPlayback does not patch or stop B after its sequence is superseded', async () => {
    const h = makeHarness();
    const oldSeq = h.orchestrator.invalidatePlaybackIntent();
    await expect(h.orchestrator.switchTrack(mkTrack('b'))).resolves.toEqual({ status: 'played' });
    const stopsBefore = h.backend.stop.mock.calls.length;

    await h.orchestrator.stopInvalidatedPlayback(oldSeq);
    expect(h.backend.stop).toHaveBeenCalledTimes(stopsBefore);
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
  });

  it('waits for an older stop to settle before B may play', async () => {
    const h = makeHarness();
    const oldStopCanFinish = deferred<void>();
    let stopCalls = 0;
    let source = '';
    let oldStopSettled = false;
    h.backend.stop.mockImplementation(async () => {
      stopCalls += 1;
      if (stopCalls === 1) {
        await oldStopCanFinish.promise;
        oldStopSettled = true;
      }
      source = '';
    });
    h.backend.playUrl.mockImplementation(async (url: string) => {
      if (!oldStopSettled) throw new Error('played before old stop settled');
      source = url;
      return true;
    });

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    const b = h.orchestrator.switchTrack(mkTrack('b'));
    await vi.waitFor(() => expect(h.backend.stop).toHaveBeenCalledTimes(1));

    oldStopCanFinish.resolve();
    await expect(b).resolves.toEqual({ status: 'played' });
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(source).toBe('http://x/song.mp3');
  });

  it('does not let a stale resolved request overwrite the newer track', async () => {
    const h = makeHarness();
    const aResolve = deferred<ResolveTrackResult>();
    h.resolveTrack.mockImplementationOnce(() => aResolve.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => {
      expect(h.resolveTrack).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'a' }), '128');
    });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = await h.orchestrator.switchTrack(mkTrack('b'));

    aResolve.resolve(resolvedTrack('http://x/a.mp3'));
    await expect(a).resolves.toEqual({ status: 'superseded' });

    expect(b).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.backend.playUrl).not.toHaveBeenCalledWith('http://x/a.mp3');
  });

  it('does not let a stale playUrl completion overwrite the newer track', async () => {
    const h = makeHarness();
    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/a.mp3'));
    const aPlay = deferred<boolean>();
    h.backend.playUrl.mockImplementationOnce(() => aPlay.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => {
      expect(h.backend.playUrl).toHaveBeenCalledWith('http://x/a.mp3');
    });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = await h.orchestrator.switchTrack(mkTrack('b'));

    aPlay.resolve(true);
    await expect(a).resolves.toEqual({ status: 'superseded' });

    expect(b).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
  });

  it('preserves B audio/state/history when A source preparation resolves late', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const aPrepared = deferred<{ url: string; crossOriginSafe: boolean }>();
    const prepareSourceUrl = vi.fn((url: string) =>
      url.endsWith('/a')
        ? aPrepared.promise
        : Promise.resolve({ url: 'http://127.0.0.1/b', crossOriginSafe: true }),
    );
    const backend = new Html5AudioBackend(audio, { prepareSourceUrl });
    const state = makeState();
    const uploadPlayHistory = vi.fn();
    const recordRecentPlayed = vi.fn();
    const recordDiagnostic = vi.fn();
    const orchestrator = new PlaybackOrchestrator({
      backend: () => backend,
      playSession: { skip: vi.fn(), intend: vi.fn() },
      resolveTrack: vi.fn(async (track: Track) => resolvedTrack(`https://cdn.example/${track.FileHash}`)),
      fetchCover: vi.fn(async () => null),
      uploadPlayHistory,
      recordRecentPlayed,
      recordDiagnostic,
      getState: () => state,
      patchState: (patch) => Object.assign(state, patch),
      saveQueue: vi.fn(),
    });

    const playA = orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => expect(prepareSourceUrl).toHaveBeenCalledWith('https://cdn.example/a'));

    const playB = orchestrator.switchTrack(mkTrack('b'));
    await expect(playB).resolves.toEqual({ status: 'played' });
    expect(state.currentTrack?.FileHash).toBe('b');
    expect(state.currentIndex).toBe(1);
    expect(state.isLoading).toBe(false);

    aPrepared.resolve({ url: 'http://127.0.0.1/a', crossOriginSafe: true });
    const resultA = await playA;
    expect.soft(resultA).toEqual({ status: 'superseded' });

    expect.soft(audio.src).toContain('/b');
    expect.soft(state.currentTrack?.FileHash).toBe('b');
    expect.soft(state.currentIndex).toBe(1);
    expect.soft(state.isLoading).toBe(false);
    expect.soft(uploadPlayHistory).toHaveBeenCalledTimes(1);
    expect.soft(uploadPlayHistory).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'b' }));
    expect.soft(uploadPlayHistory).not.toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'a' }));
    expect.soft(recordRecentPlayed).toHaveBeenCalledTimes(1);
    expect.soft(recordRecentPlayed).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'b' }));
    expect.soft(recordDiagnostic).toHaveBeenCalledWith(
      expect.objectContaining({
        kind: 'track_switch',
        phase: 'noop',
        detail: expect.stringContaining('superseded'),
        trackKey: 'a',
      }),
    );
    expect.soft(recordDiagnostic).not.toHaveBeenCalledWith(
      expect.objectContaining({ kind: 'media_event', phase: 'fail' }),
    );
  });

  it('does not rollback or skip the active session when a stale request fails', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });
    const aResolve = deferred<ResolveTrackResult>();
    h.resolveTrack.mockImplementationOnce(() => aResolve.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => {
      expect(h.resolveTrack).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'a' }), '128');
    });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = await h.orchestrator.switchTrack(mkTrack('b'));

    aResolve.reject(new Error('vip'));
    await expect(a).resolves.toEqual({ status: 'superseded' });

    expect(b).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.errorMsg).toBe('');
    expect(calls.filter((c) => c === 'skip')).toHaveLength(2);
  });

  it('switchQuality uses cached quality url without queue insertion, cover fetch, or history upload', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });
    const current = mkTrack('h1');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.state.currentTime = 37;
    h.state.isPlaying = true;
    h.state.availableQualities = [{ quality: '320', url: 'http://x/320.mp3' }];
    h.backend.switchUrl.mockImplementation(async (url: string, options: any) => {
      calls.push(`switchUrl:${url}:${options.position}:${options.autoplay}`);
      return true;
    });

    const result = await h.orchestrator.switchQuality('320');

    expect(result).toEqual({ status: 'played' });
    expect(calls).toEqual([
      'skip',
      'intend:h1',
      'switchUrl:http://x/320.mp3:37:true',
    ]);
    expect(h.state.currentTrack).toBe(current);
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.currentTime).toBe(37);
    expect(h.fetchCover).not.toHaveBeenCalled();
    expect(h.uploadPlayHistory).not.toHaveBeenCalled();
    expect(h.saveQueue).not.toHaveBeenCalled();
  });

  it('switchQuality resolves target quality without track transition side effects', async () => {
    const h = makeHarness();
    const current = mkTrack('h1');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.state.currentTime = 12;
    h.state.isPlaying = false;
    h.state.availableQualities = [];
    h.resolveTrack.mockResolvedValueOnce({
      status: 1,
      url: 'http://x/flac.mp3',
      data: { available_qualities: [{ quality: 'flac', url: 'http://x/flac.mp3' }] },
    });
    h.backend.switchUrl.mockResolvedValueOnce(true);

    const result = await h.orchestrator.switchQuality('flac');

    expect(result).toEqual({ status: 'played' });
    expect(h.resolveTrack).toHaveBeenCalledWith(current, 'flac');
    expect(h.backend.switchUrl).toHaveBeenCalledWith('http://x/flac.mp3', {
      position: 12,
      autoplay: false,
    });
    expect(h.state.currentTrack).toBe(current);
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.currentTime).toBe(12);
    expect(h.fetchCover).not.toHaveBeenCalled();
    expect(h.uploadPlayHistory).not.toHaveBeenCalled();
    expect(h.saveQueue).not.toHaveBeenCalled();
  });

  it('reports superseded without an error when a track switch overtakes a quality reload', async () => {
    const h = makeHarness();
    const current = mkTrack('a');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    const qualityResolve = deferred<ResolveTrackResult>();
    h.resolveTrack.mockImplementationOnce(() => qualityResolve.promise);

    const qualityChange = h.orchestrator.switchQuality('flac');
    await vi.waitFor(() => expect(h.resolveTrack).toHaveBeenCalledWith(current, 'flac'));

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    await expect(h.orchestrator.switchTrack(mkTrack('b'))).resolves.toEqual({ status: 'played' });

    qualityResolve.resolve(resolvedTrack('http://x/a.flac'));
    await expect(qualityChange).resolves.toEqual({ status: 'superseded' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.errorMsg).toBe('');
    expect(h.backend.switchUrl).not.toHaveBeenCalled();
    expect(h.recordDiagnostic).toHaveBeenCalledWith(
      expect.objectContaining({
        kind: 'track_switch',
        phase: 'noop',
        detail: expect.stringContaining('superseded'),
        trackKey: 'a',
      }),
    );
    expect(h.recordDiagnostic).not.toHaveBeenCalledWith(
      expect.objectContaining({ kind: 'media_event', phase: 'fail' }),
    );
  });

  it('resumeOrReloadCurrent reloads via switchTrack when backend has no source', async () => {
    const h = makeHarness();
    const current = mkTrack('h1');
    h.state.currentTrack = current;
    h.state.queue = [current];
    h.state.currentIndex = 0;
    h.backend.hasSource.mockReturnValue(false);

    const result = await h.orchestrator.resumeOrReloadCurrent();

    expect(result).toEqual({ status: 'played' });
    expect(h.resolveTrack).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'h1' }), '128');
    expect(h.backend.resume).not.toHaveBeenCalled();
  });

  it('resumeOrReloadCurrent resumes when backend has source', async () => {
    const h = makeHarness();
    h.state.currentTrack = mkTrack('h1');
    h.backend.hasSource.mockReturnValue(true);

    const result = await h.orchestrator.resumeOrReloadCurrent();

    expect(result).toEqual({ status: 'played' });
    expect(h.backend.resume).toHaveBeenCalled();
    expect(h.resolveTrack).not.toHaveBeenCalled();
  });

  it('replaySameTrack intends before seek and resume', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });
    const current = mkTrack('h1');
    h.state.currentTrack = current;

    const result = await h.orchestrator.replaySameTrack();

    expect(result).toEqual({ status: 'played' });
    expect(calls).toEqual(['intend:h1', 'seek:0', 'resume']);
  });

  it.each([
    { oldStop: 'switchTrack', nextIntent: 'quality' },
    { oldStop: 'cancel', nextIntent: 'resume' },
    { oldStop: 'clear', nextIntent: 'replay' },
  ] as const)(
    'skips a queued-before-start $oldStop stop when a newer $nextIntent intent owns playback',
    async ({ oldStop, nextIntent }) => {
      const h = makeHarness();
      const current = mkTrack('current');
      h.state.currentTrack = current;
      h.state.currentIndex = 0;
      h.state.queue = [current];
      h.state.currentTime = 37;
      h.state.isPlaying = nextIntent !== 'resume';
      h.state.availableQualities = [
        { quality: '128', url: 'http://x/current.mp3' },
        { quality: '320', url: 'http://x/quality.mp3' },
      ];

      let source = 'http://x/current.mp3';
      let playing = true;
      let position = 37;
      let stopCalls = 0;
      let destructiveStops = 0;
      const tailGateStarted = deferred<void>();
      const releaseTailGate = deferred<void>();
      h.backend.stop.mockImplementation(async () => {
        stopCalls += 1;
        if (stopCalls === 1) {
          tailGateStarted.resolve();
          await releaseTailGate.promise;
          return;
        }
        destructiveStops += 1;
        source = '';
        playing = false;
      });
      h.backend.hasSource.mockImplementation(() => source !== '');
      h.backend.switchUrl.mockImplementation(async (url, options) => {
        source = url;
        playing = options.autoplay;
        position = options.position ?? 0;
        return true;
      });
      h.backend.resume.mockImplementation(async () => { playing = true; });
      h.backend.seek.mockImplementation(async (seconds) => { position = seconds; });

      // A first stop callback occupies stopTail. The matrix stop is queued
      // behind it but has not entered backend.stop when the newer intent wins.
      const tailHolder = h.orchestrator.cancelPendingPlayback();
      await tailGateStarted.promise;
      const obsolete = oldStop === 'switchTrack'
        ? h.orchestrator.switchTrack(mkTrack('obsolete'))
        : oldStop === 'cancel'
          ? h.orchestrator.cancelPendingPlayback()
          : h.orchestrator.clearCurrentPlayback();
      const next = nextIntent === 'quality'
        ? h.orchestrator.switchQuality('320')
        : nextIntent === 'resume'
          ? h.orchestrator.resumeOrReloadCurrent()
          : h.orchestrator.replaySameTrack();

      expect(h.backend.stop).toHaveBeenCalledTimes(1);
      releaseTailGate.resolve();
      await tailHolder;
      await obsolete;
      await expect(next).resolves.toEqual({ status: 'played' });

      expect(destructiveStops, 'the stale queued stop callback must be skipped').toBe(0);
      expect(h.backend.stop).toHaveBeenCalledTimes(1);
      expect(h.state.currentTrack?.FileHash).toBe('current');
      expect(source).not.toBe('');
      expect(playing).toBe(true);
      if (nextIntent === 'quality') {
        expect(source).toBe('http://x/quality.mp3');
        expect(position).toBe(37);
      } else if (nextIntent === 'resume') {
        expect(h.backend.resume).toHaveBeenCalledTimes(1);
      } else {
        expect(h.backend.seek).toHaveBeenCalledWith(0);
        expect(h.backend.resume).toHaveBeenCalledTimes(1);
        expect(position).toBe(0);
      }
    },
  );

  it.each([
    { oldStop: 'switchTrack', nextIntent: 'quality' },
    { oldStop: 'cancel', nextIntent: 'resume' },
    { oldStop: 'clear', nextIntent: 'replay' },
  ] as const)(
    'waits for an in-flight destructive $oldStop stop before applying a newer $nextIntent intent',
    async ({ oldStop, nextIntent }) => {
      const h = makeHarness();
      const current = mkTrack('current');
      h.state.currentTrack = current;
      h.state.currentIndex = 0;
      h.state.queue = [current];
      h.state.currentTime = 37;
      h.state.isPlaying = nextIntent !== 'resume';
      h.state.availableQualities = [
        { quality: '128', url: 'http://x/current.mp3' },
        { quality: '320', url: 'http://x/quality.mp3' },
      ];

      let source = 'http://x/current.mp3';
      let playing = true;
      let position = 37;
      let destructiveStops = 0;
      const switchUrlCalls: Array<{
        url: string;
        options: { position?: number; autoplay: boolean };
      }> = [];
      const stopStarted = deferred<void>();
      const releaseStop = deferred<void>();
      h.backend.stop.mockImplementation(async () => {
        stopStarted.resolve();
        await releaseStop.promise;
        destructiveStops += 1;
        source = '';
        playing = false;
      });
      h.backend.hasSource.mockImplementation(() => source !== '');
      h.backend.playUrl.mockImplementation(async (url) => {
        source = url;
        playing = true;
        return true;
      });
      h.backend.switchUrl.mockImplementation(async (url, options) => {
        switchUrlCalls.push({ url, options: { ...options } });
        source = url;
        playing = options.autoplay;
        position = options.position ?? 0;
        return true;
      });
      h.backend.resume.mockImplementation(async () => {
        if (!source) throw new Error('cannot resume without a source');
        playing = true;
      });
      h.backend.seek.mockImplementation(async () => {
        if (!source) throw new Error('cannot seek without a source');
      });

      const obsolete = oldStop === 'switchTrack'
        ? h.orchestrator.switchTrack(mkTrack('obsolete'))
        : oldStop === 'cancel'
          ? h.orchestrator.cancelPendingPlayback()
          : h.orchestrator.clearCurrentPlayback();
      await stopStarted.promise;
      const next = nextIntent === 'quality'
        ? h.orchestrator.switchQuality('320')
        : nextIntent === 'resume'
          ? h.orchestrator.resumeOrReloadCurrent()
          : h.orchestrator.replaySameTrack();

      await Promise.resolve();
      expect(h.backend.switchUrl, 'quality must not switch during the old stop').not.toHaveBeenCalled();
      expect(h.backend.resume, 'resume must not run during the old stop').not.toHaveBeenCalled();
      expect(h.backend.seek, 'replay must not seek during the old stop').not.toHaveBeenCalled();
      expect(h.backend.playUrl, 'reload must not start during the old stop').not.toHaveBeenCalled();

      releaseStop.resolve();
      await obsolete;
      await expect(next).resolves.toEqual({ status: 'played' });

      expect(destructiveStops).toBe(1);
      expect(h.state.currentTrack?.FileHash).toBe('current');
      expect(source).not.toBe('');
      expect(playing).toBe(true);
      if (nextIntent === 'quality') {
        expect(source).toBe('http://x/quality.mp3');
        expect(switchUrlCalls).toEqual([{
          url: 'http://x/quality.mp3',
          options: { position: 37, autoplay: true },
        }]);
        expect(position).toBe(37);
      } else if (nextIntent === 'resume') {
        expect(source).toBe('http://x/current.mp3');
        expect(switchUrlCalls).toEqual([{
          url: 'http://x/current.mp3',
          options: { position: 37, autoplay: true },
        }]);
        expect(position).toBe(37);
      } else {
        expect(source).toBe('http://x/current.mp3');
        expect(switchUrlCalls).toEqual([{
          url: 'http://x/current.mp3',
          options: { position: 0, autoplay: true },
        }]);
        expect(position).toBe(0);
      }
      expect(h.backend.playUrl).not.toHaveBeenCalled();
      expect(h.uploadPlayHistory).not.toHaveBeenCalled();
      expect(h.recordRecentPlayed).not.toHaveBeenCalled();
      expect(h.saveQueue).not.toHaveBeenCalled();
    },
  );

  it('cancelPendingPlayback during a deferred playUrl skips the session, stops the backend, and clears loading state', async () => {
    // T1: a slow playUrl (gated on an external release) simulates a network
    // load in flight. cancelPendingPlayback() must skip the session, stop the
    // backend, and flip isLoading/isPlaying off — and the late-resolving
    // playUrl must NOT corrupt the canceled state (it lands as superseded).
    const calls: string[] = [];
    const h = makeHarness({ calls });

    // Gate playUrl so it stays pending until we release it.
    const playUrlCanFinish = deferred<void>();
    const playUrlStarted = deferred<void>();
    h.backend.playUrl.mockImplementation(async (url: string) => {
      calls.push(`playUrl:${url}`);
      playUrlStarted.resolve();
      await playUrlCanFinish.promise;
      return true;
    });

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await playUrlStarted.promise;

    // While playUrl is in flight, the user cancels.
    await h.orchestrator.cancelPendingPlayback();

    // Session was skipped (twice: once for switchTrack, once for cancel) and
    // backend.stop was called (same pattern).
    expect(h.playSession.skip).toHaveBeenCalledTimes(2);
    expect(h.backend.stop).toHaveBeenCalled();
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);

    // Release the late playUrl; the original switchTrack must resolve superseded
    // and must NOT flip state back to playing/loading.
    playUrlCanFinish.resolve();
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
  });

  it('clearCurrentPlayback invalidates a pending resolve and leaves the player idle', async () => {
    const h = makeHarness();
    const resolveA = deferred<ResolveTrackResult>();
    h.resolveTrack.mockImplementationOnce(() => resolveA.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => {
      expect(h.resolveTrack).toHaveBeenCalledWith(expect.objectContaining({ FileHash: 'a' }), '128');
    });
    h.state.errorMsg = '正在加载音频源…';

    await h.orchestrator.clearCurrentPlayback();

    expect(h.playSession.skip).toHaveBeenCalledTimes(2);
    expect(h.backend.stop).toHaveBeenCalledTimes(2);
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
    expect(h.state.errorMsg).toBe('');

    const stopCountBefore = h.backend.stop.mock.calls.length;
    resolveA.resolve(resolvedTrack('http://x/a.mp3'));
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.backend.playUrl).not.toHaveBeenCalled();
    expect(h.uploadPlayHistory).not.toHaveBeenCalled();
    expect(h.recordRecentPlayed).not.toHaveBeenCalled();
    expect(h.backend.stop).toHaveBeenCalledTimes(stopCountBefore);
    expect(h.state.isLoading).toBe(false);
  });

  it('does not let a clearCurrentPlayback completion patch B when B takes ownership while stop is pending', async () => {
    const h = makeHarness();
    const resolveA = deferred<ResolveTrackResult>();
    const clearStopCanFinish = deferred<void>();
    let stopCount = 0;
    let clearStopStarted = false;
    h.backend.stop.mockImplementation(async () => {
      stopCount += 1;
      if (stopCount === 2) {
        clearStopStarted = true;
        await clearStopCanFinish.promise;
      }
    });
    h.resolveTrack.mockImplementationOnce(() => resolveA.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => expect(h.resolveTrack).toHaveBeenCalledTimes(1));
    const clear = h.orchestrator.clearCurrentPlayback();
    await Promise.resolve();
    expect(clearStopStarted).toBe(true);
    if (!clearStopStarted) return;

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = h.orchestrator.switchTrack(mkTrack('b'));
    clearStopCanFinish.resolve();
    await clear;
    await expect(b).resolves.toEqual({ status: 'played' });
    const stopCountBefore = h.backend.stop.mock.calls.length;
    resolveA.resolve(resolvedTrack('http://x/a.mp3'));
    await expect(a).resolves.toEqual({ status: 'superseded' });

    expect(h.backend.stop).toHaveBeenCalledTimes(stopCountBefore);
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
    expect(h.state.errorMsg).toBe('');
  });

  it('switchTrack(B) started during cancelPendingPlayback\'s await backend.stop plays B without re-stopping on A\'s late resolve', async () => {
    // T2 (review gap #2): the hard race. switchTrack(A) has playUrl in flight.
    // cancelPendingPlayback starts and is suspended inside `await backend.stop()`.
    // WHILE that stop is pending, switchTrack(B) begins. When A's late playUrl
    // finally resolves, its superseded completion must not call backend.stop
    // again or clobber B's state. B ends up playing.
    const calls: string[] = [];
    const h = makeHarness({ calls });

    // Gates for each async suspension point, indexed by stop-call order.
    const aPlayUrlStarted = deferred<void>();
    const aPlayUrlCanFinish = deferred<void>();
    const cancelStopStarted = deferred<void>();
    const cancelStopCanFinish = deferred<void>();
    const bStopStarted = deferred<void>();
    const bStopCanFinish = deferred<void>();

    // stop() is called in a known order: 1) switchTrack(A), 2) cancel, 3) switchTrack(B).
    // We gate #2 and #3 so we can park cancel and B independently.
    let stopCount = 0;
    h.backend.stop.mockImplementation(async () => {
      stopCount += 1;
      calls.push(`stop#${stopCount}`);
      if (stopCount === 2) {
        cancelStopStarted.resolve();
        await cancelStopCanFinish.promise;
      } else if (stopCount === 3) {
        bStopStarted.resolve();
        await bStopCanFinish.promise;
      }
    });

    h.backend.playUrl.mockImplementation(async (url: string) => {
      calls.push(`playUrl:${url}`);
      if (url.includes('/a.mp3')) {
        aPlayUrlStarted.resolve();
        await aPlayUrlCanFinish.promise;
      }
      return true;
    });

    // 1. switchTrack(A): stop#1 (immediate) → resolve → intend → playUrl(A) [gated].
    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/a.mp3'));
    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await aPlayUrlStarted.promise;
    expect(h.backend.playUrl).toHaveBeenCalledWith('http://x/a.mp3');

    // 2. cancelPendingPlayback: bumps transitionSeq to 2, then awaits
    //    backend.stop() → stop#2 (gated). Parked here.
    const cancelPromise = h.orchestrator.cancelPendingPlayback();
    await cancelStopStarted.promise;

    // 3. B queues its stop behind cancel's pending stop.
    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = h.orchestrator.switchTrack(mkTrack('b'));

    // 4. Release cancel's stop. cancel's `isCurrent(seq=2)` check fails
    //    (transitionSeq is now 3) → cancel returns WITHOUT patching state.
    cancelStopCanFinish.resolve();
    await cancelPromise;
    await bStopStarted.promise;

    // 5. Release B's stop. B proceeds: resolve(B) → intend(B) → playUrl(B) → played.
    bStopCanFinish.resolve();
    const bResult = await b;
    expect(bResult).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);

    // 6. Release A's late playUrl. It resolves superseded without an extra
    //    backend.stop or state clobber.
    const stopCallsBefore = h.backend.stop.mock.calls.length;
    aPlayUrlCanFinish.resolve();
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.backend.stop.mock.calls.length, 'superseded A must not re-stop B').toBe(stopCallsBefore);

    // B is still current, not clobbered. Phase authority projects isPlaying
    // from playbackPhase='playing' after a successful switchTrack.
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
    expect(h.state.playbackPhase).toBe('playing');
    expect(h.state.isPlaying).toBe(true);
  });

  it('a superseded playUrl completion without a prior cancel does not re-stop B', async () => {
    // T3: two switchTracks in succession. A's playUrl is deferred, B starts
    // and plays, then A resolves superseded without stopping or patching B.
    const calls: string[] = [];
    const h = makeHarness({ calls });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/a.mp3'));
    const aPlay = deferred<boolean>();
    h.backend.playUrl.mockImplementationOnce(() => aPlay.promise);

    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await vi.waitFor(() => {
      expect(h.backend.playUrl).toHaveBeenCalledWith('http://x/a.mp3');
    });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = await h.orchestrator.switchTrack(mkTrack('b'));
    expect(b).toEqual({ status: 'played' });

    // Snapshot stop count after B has fully played.
    const stopCountBefore = h.backend.stop.mock.calls.length;

    // Release A's late playUrl → A resolves superseded.
    aPlay.resolve(true);
    await expect(a).resolves.toEqual({ status: 'superseded' });

    // No extra stop or state clobber.
    expect(h.backend.stop.mock.calls.length, 'superseded A must not re-stop B').toBe(stopCountBefore);
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
  });

  it('double cancel during loading is a no-op the second time and leaves a clean idle state', async () => {
    // T4: user clicks pause twice while a track is loading. The first cancel
    // increments the transition epoch; the second cancel must not crash,
    // corrupt state, or leave isLoading/isPlaying enabled.
    const calls: string[] = [];
    const h = makeHarness({ calls });

    const aPlayUrlStarted = deferred<void>();
    const aPlayUrlCanFinish = deferred<void>();
    h.backend.playUrl.mockImplementation(async (url: string) => {
      calls.push(`playUrl:${url}`);
      aPlayUrlStarted.resolve();
      await aPlayUrlCanFinish.promise;
      return true;
    });

    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/a.mp3'));
    const a = h.orchestrator.switchTrack(mkTrack('a'));
    await aPlayUrlStarted.promise;

    // Two cancels in succession (the second starts after the first completes).
    await h.orchestrator.cancelPendingPlayback();
    await h.orchestrator.cancelPendingPlayback();

    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);

    // Releasing the late playUrl must not flip state back.
    aPlayUrlCanFinish.resolve();
    await expect(a).resolves.toEqual({ status: 'superseded' });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
  });
});

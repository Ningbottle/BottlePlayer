import { describe, expect, it, vi } from 'vitest';
import type { Track } from '../normalizer';
import {
  PlaybackOrchestrator,
  type PlaybackStateSlice,
  type ResolveTrackResult,
} from '../playbackOrchestrator';

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
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    quality: '128',
    availableQualities: [],
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
  const saveQueue = vi.fn(() => {
    calls.push('saveQueue');
  });

  const orchestrator = new PlaybackOrchestrator({
    backend: () => backend,
    playSession,
    resolveTrack,
    fetchCover,
    uploadPlayHistory,
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
  it('switchTrack orders skip, Resolve, intend, playUrl, saveQueue', async () => {
    const calls: string[] = [];
    const h = makeHarness({ calls });

    const result = await h.orchestrator.switchTrack(mkTrack('h1'));

    expect(result).toEqual({ status: 'played' });
    expect(calls).toEqual([
      'skip',
      'stop',
      'resolve:h1',
      'intend:h1',
      'playUrl:http://x/song.mp3',
      'saveQueue',
      'upload:h1',
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

    expect(result).toEqual({ status: 'failed', error: 'vip' });
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

    expect(result).toEqual({ status: 'failed', error: '播放失败' });
    expect(h.state.currentIndex).toBe(0);
    expect(h.state.currentTrack?.FileHash).toBe('good');
    expect(calls).toEqual([
      'skip',
      'stop',
      'resolve:bad',
      'intend:bad',
      'playUrl:http://x/song.mp3',
      'skip',
      'saveQueue',
    ]);
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
    await expect(a).resolves.toEqual({ status: 'stale' });

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
    await expect(a).resolves.toEqual({ status: 'stale' });

    expect(b).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
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
    await expect(a).resolves.toEqual({ status: 'stale' });

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
});

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
    isLoading: false,
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

  it('cancelPendingPlayback during a deferred playUrl skips the session, stops the backend, and clears loading state', async () => {
    // T1: a slow playUrl (gated on an external release) simulates a network
    // load in flight. cancelPendingPlayback() must skip the session, stop the
    // backend, and flip isLoading/isPlaying off — and the late-resolving
    // playUrl must NOT corrupt the canceled state (it lands as 'stale' and the
    // stale-cleanup guard bails rather than re-stopping).
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

    // Release the late playUrl; the original switchTrack must resolve 'stale'
    // and must NOT flip state back to playing/loading.
    playUrlCanFinish.resolve();
    await expect(a).resolves.toEqual({ status: 'stale' });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
  });

  it('switchTrack(B) started during cancelPendingPlayback\'s await backend.stop plays B without re-stopping on A\'s late resolve', async () => {
    // T2 (review gap #2): the hard race. switchTrack(A) has playUrl in flight.
    // cancelPendingPlayback starts and is suspended inside `await backend.stop()`.
    // WHILE that stop is pending, switchTrack(B) begins. When A's late playUrl
    // finally resolves, cleanupCanceledStaleTransition must bail on guard (B)
    // (transitionSeq !== canceledThroughSeq + 1, because B bumped transitionSeq
    // to canceledThroughSeq + 2) — so it does NOT call backend.stop again and
    // does NOT clobber B's state. B ends up playing.
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

    // 2. cancelPendingPlayback: bumps transitionSeq to 2, canceledThroughSeq=1,
    //    then `await backend.stop()` → stop#2 (gated). Parked here.
    const cancelPromise = h.orchestrator.cancelPendingPlayback();
    await cancelStopStarted.promise;

    // 3. While cancel is parked, start switchTrack(B): bumps transitionSeq to 3,
    //    then `await backend.stop()` → stop#3 (gated). Parked here.
    h.resolveTrack.mockResolvedValueOnce(resolvedTrack('http://x/b.mp3'));
    const b = h.orchestrator.switchTrack(mkTrack('b'));
    await bStopStarted.promise;

    // 4. Release cancel's stop. cancel's `isCurrent(seq=2)` check fails
    //    (transitionSeq is now 3) → cancel returns WITHOUT patching state.
    cancelStopCanFinish.resolve();
    await cancelPromise;

    // 5. Release B's stop. B proceeds: resolve(B) → intend(B) → playUrl(B) → played.
    bStopCanFinish.resolve();
    const bResult = await b;
    expect(bResult).toEqual({ status: 'played' });
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);

    // 6. Release A's late playUrl. A resolves 'stale'. The stale path calls
    //    cleanupCanceledStaleTransition(A's seq=1):
    //      guard (A) seq(1) > canceledThroughSeq(1)? no.
    //      guard (B) transitionSeq(3) !== canceledThroughSeq(1)+1(2)? YES → bail.
    //    So NO extra backend.stop and NO state clobber.
    const stopCallsBefore = h.backend.stop.mock.calls.length;
    aPlayUrlCanFinish.resolve();
    await expect(a).resolves.toEqual({ status: 'stale' });
    expect(h.backend.stop.mock.calls.length, 'cleanup must not re-stop on A\'s late resolve').toBe(stopCallsBefore);

    // B is still current, not clobbered.
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
  });

  it('a stale playUrl completion without a prior cancel bails cleanup on guard (A) and does not re-stop', async () => {
    // T3: no cancel happened (canceledThroughSeq stays 0). Two switchTracks in
    // succession: A's playUrl is deferred, B starts and plays, A's playUrl then
    // late-resolves. cleanupCanceledStaleTransition(A's seq=1) must bail on
    // guard (A) — seq(1) > canceledThroughSeq(0) — so it does NOT call
    // backend.stop and does NOT patch state. B is undisturbed.
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

    // Release A's late playUrl → A resolves 'stale' → cleanup guard (A) bails.
    aPlay.resolve(true);
    await expect(a).resolves.toEqual({ status: 'stale' });

    // No extra stop from cleanup (guard (A) bail), no state clobber.
    expect(h.backend.stop.mock.calls.length, 'cleanup must not re-stop when there was no cancel').toBe(stopCountBefore);
    expect(h.state.currentTrack?.FileHash).toBe('b');
    expect(h.state.isLoading).toBe(false);
  });

  it('double cancel during loading is a no-op the second time and leaves a clean idle state', async () => {
    // T4: user clicks pause twice while a track is loading. The first cancel
    // bumps canceledThroughSeq to the pending seq and transitionSeq to N+1;
    // the second cancel must not crash, must not corrupt state, and the final
    // state must be isLoading:false, isPlaying:false.
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
    await expect(a).resolves.toEqual({ status: 'stale' });
    expect(h.state.isLoading).toBe(false);
    expect(h.state.isPlaying).toBe(false);
  });
});

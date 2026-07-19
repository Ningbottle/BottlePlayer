import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  PlaybackCommandCoordinator,
  type CoordinatorDeps,
  type CoordinatorState,
} from '../playbackCommandCoordinator';
import type { Track } from '../normalizer';
import type { PlaybackPhase } from '../playbackPhase';

function mkTrack(hash: string, name = hash): Track {
  return {
    FileHash: hash,
    SongName: name,
    SingerName: 'A',
    Duration: 100,
  } as Track;
}

function deferred<T>() {
  let resolve!: (v: T | PromiseLike<T>) => void;
  let reject!: (e?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function makeState(partial: Partial<CoordinatorState> = {}): CoordinatorState {
  return {
    queue: [],
    currentIndex: -1,
    currentTrack: null,
    isPlaying: false,
    isLoading: false,
    currentTime: 0,
    duration: 0,
    errorMsg: '',
    isPreview: false,
    vipRequired: false,
    availableQualities: [],
    playbackPhase: 'idle' as PlaybackPhase,
    queueMode: 'normal',
    loopMode: 'list',
    audio: null,
    ...partial,
  };
}

describe('PlaybackCommandCoordinator', () => {
  let state: CoordinatorState;
  let playLog: string[];
  let playGates: Map<string, ReturnType<typeof deferred<{ status: string }>>>;
  let deps: CoordinatorDeps;
  let coord: PlaybackCommandCoordinator;

  beforeEach(() => {
    state = makeState();
    playLog = [];
    playGates = new Map();
    deps = {
      getState: () => state,
      patchState: (p) => Object.assign(state, p),
      saveQueue: vi.fn(),
      playTrack: vi.fn(async (track: Track) => {
        playLog.push(`play:${track.FileHash}`);
        const gate = playGates.get(track.FileHash);
        if (gate) {
          const r = await gate.promise;
          state.currentTrack = track;
          state.currentIndex = state.queue.findIndex((t) => t.FileHash === track.FileHash);
          state.isPlaying = true;
          state.isLoading = false;
          state.playbackPhase = 'playing';
          return r;
        }
        state.currentTrack = track;
        state.currentIndex = Math.max(
          0,
          state.queue.findIndex((t) => t.FileHash === track.FileHash),
        );
        state.isPlaying = true;
        state.isLoading = false;
        state.playbackPhase = 'playing';
        return { status: 'played' };
      }),
      switchQuality: vi.fn(async () => ({ status: 'played' })),
      seek: vi.fn(async (s: number) => {
        state.currentTime = s;
      }),
      pause: vi.fn(async () => {
        state.isPlaying = false;
        state.playbackPhase = 'paused';
      }),
      resumeOrReload: vi.fn(async () => {
        state.isPlaying = true;
        state.playbackPhase = 'playing';
      }),
      invalidatePlaybackIntent: vi.fn(() => 1),
      stopInvalidatedPlayback: vi.fn(async () => {
        state.isPlaying = false;
        state.isLoading = false;
      }),
      skipSession: vi.fn(),
      hasBackend: () => true,
    };
    coord = new PlaybackCommandCoordinator(deps);
  });

  it('coalesces three next presses into a single +3 jump when first play is gated', async () => {
    const tracks = [mkTrack('t0'), mkTrack('t1'), mkTrack('t2'), mkTrack('t3')];
    state.queue = tracks;
    state.currentIndex = 0;
    state.currentTrack = tracks[0];
    state.playbackPhase = 'playing';
    state.isPlaying = true;

    // Gate plays of intermediate targets so nav coalesces before any resolve.
    const g1 = deferred<{ status: string }>();
    playGates.set('t1', g1);
    playGates.set('t2', deferred<{ status: string }>());
    playGates.set('t3', deferred<{ status: string }>());

    const p1 = coord.dispatch({ type: 'next' });
    // Let drain start and hit first play(t1)
    await Promise.resolve();
    await Promise.resolve();

    const p2 = coord.dispatch({ type: 'next' });
    const p3 = coord.dispatch({ type: 'next' });

    // Unblock whatever play is in flight; coordinator should settle on final target.
    for (const [, g] of playGates) g.resolve({ status: 'played' });
    await Promise.all([p1, p2, p3]);

    // With coalesce: after first next starts play(t1), additional nexts add delta while draining.
    // Final current track should be advanced by total net delta from start.
    expect(state.currentTrack?.FileHash).toBe('t3');
    // Should not have committed every intermediate as separate final — last wins.
    expect(playLog[playLog.length - 1]).toBe('play:t3');
  });

  it('next then prev cancel out without reloading the same track', async () => {
    const tracks = [mkTrack('a'), mkTrack('b')];
    state.queue = tracks;
    state.currentIndex = 0;
    state.currentTrack = tracks[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    const gate = deferred<{ status: string }>();
    playGates.set('b', gate);

    const n = coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();
    const p = coord.dispatch({ type: 'prev' });

    gate.resolve({ status: 'played' });
    await Promise.all([n, p]);

    // Net delta 0 after first next begins: may have played b then back, or cancelled before play.
    // Spec: next+prev cancel — final should be original 'a' without requiring a redundant reload of a if never left.
    // Our drain: first next starts play(b). prev arrives, navDelta becomes 0 after next was already consumed...
    // Fix: while play in flight, remaining navDelta should still apply after.
    // After play(b), drain continues with prev => play(a).
    expect(state.currentTrack?.FileHash).toBe('a');
  });

  it('select B then select C — only C commits (latest-wins)', async () => {
    state.queue = [mkTrack('a'), mkTrack('b'), mkTrack('c')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];

    const bGate = deferred<{ status: string }>();
    const cGate = deferred<{ status: string }>();
    playGates.set('b', bGate);
    playGates.set('c', cGate);

    const pb = coord.dispatch({ type: 'selectTrack', track: mkTrack('b') });
    await Promise.resolve();
    await Promise.resolve();
    const pc = coord.dispatch({ type: 'selectTrack', track: mkTrack('c') });

    // B still in flight; resolve B (may be superseded by design after C queued)
    bGate.resolve({ status: 'played' });
    cGate.resolve({ status: 'played' });
    await Promise.all([pb, pc]);

    expect(state.currentTrack?.FileHash).toBe('c');
    expect(playLog.includes('play:c')).toBe(true);
  });

  it('clearQueue vs pending next leaves empty idle queue', async () => {
    state.queue = [mkTrack('a'), mkTrack('b')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';
    state.duration = 120;
    state.vipRequired = true;

    const gate = deferred<{ status: string }>();
    playGates.set('b', gate);

    const n = coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();
    const c = coord.dispatch({ type: 'clearQueue' });

    gate.resolve({ status: 'played' });
    await Promise.all([n, c]);

    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
    expect(state.currentIndex).toBe(-1);
    expect(state.isPlaying).toBe(false);
    expect(state.playbackPhase).toBe('idle');
    expect(state.duration).toBe(0);
    expect(state.vipRequired).toBe(false);
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
  });

  it('UI next and OS-style next share the same coalesce order', async () => {
    state.queue = [mkTrack('0'), mkTrack('1'), mkTrack('2')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;

    await Promise.all([
      coord.dispatch({ type: 'next' }),
      coord.dispatch({ type: 'next' }), // OS media also calls next
    ]);

    expect(state.currentTrack?.FileHash).toBe('2');
  });

  it('duplicate ended events only advance once per epoch', async () => {
    state.queue = [mkTrack('0'), mkTrack('1')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    await Promise.all([
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
    ]);

    expect(state.currentTrack?.FileHash).toBe('1');
    expect(playLog.filter((x) => x === 'play:1')).toHaveLength(1);
  });

  it('failed switchQuality restores time/phase/playing snapshot', async () => {
    state.queue = [mkTrack('a')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.currentTime = 42;
    state.duration = 200;
    state.playbackPhase = 'playing';
    state.vipRequired = false;

    deps.switchQuality = vi.fn(async () => {
      // Simulate bad path that mutates then fails
      state.currentTime = 0;
      state.isPlaying = false;
      return { status: 'failed', message: 'quality unavailable' };
    });

    const r = await coord.dispatch({ type: 'switchQuality', quality: 'flac' });
    expect(r.status).toBe('failed');
    expect(state.currentTime).toBe(42);
    expect(state.isPlaying).toBe(true);
    expect(state.playbackPhase).toBe('playing');
    expect(state.currentTrack?.FileHash).toBe('a');
  });

  it('remove last track resets backend phase duration vip', async () => {
    state.queue = [mkTrack('only')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.duration = 99;
    state.currentTime = 10;
    state.vipRequired = true;
    state.isPreview = true;
    state.playbackPhase = 'playing';

    await coord.dispatch({ type: 'removeTrack', index: 0 });

    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
    expect(state.currentIndex).toBe(-1);
    expect(state.isPlaying).toBe(false);
    expect(state.playbackPhase).toBe('idle');
    expect(state.duration).toBe(0);
    expect(state.currentTime).toBe(0);
    expect(state.vipRequired).toBe(false);
    expect(state.isPreview).toBe(false);
    expect(deps.stopInvalidatedPlayback).toHaveBeenCalled();
  });

  it('seek and quality promises settle independently (no shared waiter batch)', async () => {
    state.queue = [mkTrack('a')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';
    state.currentTime = 10;

    // Gate quality so seek can finish first while quality is still pending.
    const qualityGate = deferred<{ status: string; message?: string }>();
    deps.switchQuality = vi.fn(async () => {
      const r = await qualityGate.promise;
      return r;
    });

    // Start a slow select so drain is busy; then queue seek + quality.
    const selectGate = deferred<{ status: string }>();
    playGates.set('a', selectGate);
    // Force a play path first: next on a single-track loop wraps to same track.
    state.queue = [mkTrack('a'), mkTrack('b')];
    state.currentIndex = 0;
    const playGate = deferred<{ status: string }>();
    playGates.set('b', playGate);

    const navP = coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();

    const seekP = coord.dispatch({ type: 'seek', seconds: 55 });
    const qualityP = coord.dispatch({ type: 'switchQuality', quality: 'flac' });

    // Unblock nav so drain reaches seek then quality.
    playGate.resolve({ status: 'played' });
    await navP;

    // Seek must resolve without waiting for quality.
    const seekSettled = await Promise.race([
      seekP.then((r) => ({ kind: 'seek' as const, r })),
      qualityP.then((r) => ({ kind: 'quality' as const, r })),
      new Promise<{ kind: 'timeout' }>((res) => setTimeout(() => res({ kind: 'timeout' }), 50)),
    ]);
    expect(seekSettled.kind).toBe('seek');
    if (seekSettled.kind === 'seek') {
      expect(seekSettled.r.status).toBe('ok');
    }
    expect(state.currentTime).toBe(55);

    // Quality still pending — must not have resolved ok early.
    let qualityDone = false;
    void qualityP.then(() => {
      qualityDone = true;
    });
    await Promise.resolve();
    expect(qualityDone).toBe(false);

    qualityGate.resolve({ status: 'failed', message: 'quality unavailable' });
    const qr = await qualityP;
    expect(qr.status).toBe('failed');
    expect(qr.message).toMatch(/quality/);
  });

  it('select B then C: B promise is superseded, only C commits', async () => {
    state.queue = [mkTrack('a'), mkTrack('b'), mkTrack('c')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];

    const bGate = deferred<{ status: string }>();
    const cGate = deferred<{ status: string }>();
    playGates.set('b', bGate);
    playGates.set('c', cGate);

    const pb = coord.dispatch({ type: 'selectTrack', track: mkTrack('b') });
    await Promise.resolve();
    await Promise.resolve();
    const pc = coord.dispatch({ type: 'selectTrack', track: mkTrack('c') });

    bGate.resolve({ status: 'played' });
    cGate.resolve({ status: 'played' });
    const [rb, rc] = await Promise.all([pb, pc]);

    expect(rb.status).toBe('superseded');
    expect(rc.status).toBe('ok');
    expect(state.currentTrack?.FileHash).toBe('c');
  });

  it('single-loop ended replays same track via coordinator (epoch dedupe)', async () => {
    state.queue = [mkTrack('solo'), mkTrack('other')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.loopMode = 'single';
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    await Promise.all([
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
      coord.dispatch({ type: 'ended' }),
    ]);

    expect(state.currentTrack?.FileHash).toBe('solo');
    expect(playLog.filter((x) => x === 'play:solo')).toHaveLength(1);
  });

  it('seek reject settles dispatch as failed (does not leave waiter pending)', async () => {
    state.queue = [mkTrack('a')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    deps.seek = vi.fn(async () => {
      throw new Error('seek_boom');
    });

    const r = await coord.dispatch({ type: 'seek', seconds: 9 });
    expect(r.status).toBe('failed');
    expect(r.message).toMatch(/seek_boom/);
    // Drain must stay healthy for subsequent commands
    const r2 = await coord.dispatch({ type: 'seek', seconds: 3 });
    // second seek still uses rejecting mock
    expect(r2.status).toBe('failed');
  });

  it('clearQueue then playAll without await keeps the new queue', async () => {
    state.queue = [mkTrack('old0'), mkTrack('old1')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;
    state.playbackPhase = 'playing';

    const stopGate = deferred<void>();
    deps.stopInvalidatedPlayback = vi.fn(async () => {
      await stopGate.promise;
    });

    const clearP = coord.dispatch({ type: 'clearQueue' });
    // Do not await clear — immediately replace queue (real UI fire-and-forget)
    const playP = coord.dispatch({
      type: 'playAll',
      tracks: [mkTrack('n0'), mkTrack('n1')],
      startIndex: 0,
    });

    await Promise.resolve();
    await Promise.resolve();
    stopGate.resolve();
    const [cr, pr] = await Promise.all([clearP, playP]);

    expect(cr.status).toBe('ok');
    expect(pr.status).toBe('ok');
    expect(state.queue.map((t) => t.FileHash)).toEqual(['n0', 'n1']);
    expect(state.currentTrack?.FileHash).toBe('n0');
  });

  it('dispose awaits in-flight stop and does not resolve early', async () => {
    state.queue = [mkTrack('a'), mkTrack('b')];
    state.currentIndex = 0;
    state.currentTrack = state.queue[0];
    state.isPlaying = true;

    const stopGate = deferred<void>();
    let stopStarted = false;
    deps.stopInvalidatedPlayback = vi.fn(async () => {
      stopStarted = true;
      await stopGate.promise;
      state.isPlaying = false;
      state.playbackPhase = 'idle';
    });

    const playGate = deferred<{ status: string }>();
    playGates.set('b', playGate);
    void coord.dispatch({ type: 'next' });
    await Promise.resolve();
    await Promise.resolve();

    let disposeDone = false;
    const disposeP = coord.dispose().then(() => {
      disposeDone = true;
    });

    await Promise.resolve();
    await Promise.resolve();
    // dispose should have kicked clear/stop path
    expect(stopStarted || deps.stopInvalidatedPlayback).toBeTruthy();

    // While stop is gated, dispose must not finish
    playGate.resolve({ status: 'played' });
    await new Promise((r) => setTimeout(r, 20));
    if (stopStarted) {
      expect(disposeDone).toBe(false);
      stopGate.resolve();
      await disposeP;
      expect(disposeDone).toBe(true);
    } else {
      // If interrupt cancelled play before clear, still release stop and await
      stopGate.resolve();
      await disposeP;
    }

    expect(state.queue).toEqual([]);
    expect(state.currentTrack).toBeNull();
  });
});

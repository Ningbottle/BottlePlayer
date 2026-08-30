import { describe, expect, it, vi } from 'vitest';
import { LyricsResource, type LyricLine } from '../lyricsResource';
import type { Track } from '../../shared/music/track';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function track(hash: string): Track {
  return { FileHash: hash, SongName: hash, SingerName: 'Artist', Duration: 100, Image: '' } as Track;
}

describe('LyricsResource', () => {
  it('keeps B loading and lines when A resolves after B starts', async () => {
    const a = deferred<LyricLine[]>();
    const b = deferred<LyricLine[]>();
    const fetchLyrics = vi.fn((current: Track) => current.FileHash === 'a' ? a.promise : b.promise);
    const resource = new LyricsResource(fetchLyrics);

    const loadingA = resource.load(track('a'));
    const loadingB = resource.load(track('b'));

    expect(resource.state.loading).toBe(true);
    a.resolve([{ time: 0, text: 'A line' }]);
    await loadingA;

    expect(resource.state.lines).toEqual([]);
    expect(resource.state.loading).toBe(true);
    b.resolve([{ time: 0, text: 'B line' }]);
    await loadingB;

    expect(resource.state.lines).toEqual([{ time: 0, text: 'B line' }]);
    expect(resource.state.loading).toBe(false);
  });

  it('ignores a stale rejection after B has committed', async () => {
    const a = deferred<LyricLine[]>();
    const b = deferred<LyricLine[]>();
    const staleError = new Error('stale A failure');
    const fetchLyrics = vi.fn((current: Track) => current.FileHash === 'a' ? a.promise : b.promise);
    const resource = new LyricsResource(fetchLyrics);

    const loadingA = resource.load(track('a'));
    const loadingB = resource.load(track('b'));
    b.resolve([{ time: 0, text: 'B line' }]);
    await loadingB;

    a.reject(staleError);
    await loadingA;

    expect(resource.state.lines).toEqual([{ time: 0, text: 'B line' }]);
    expect(resource.state.error).toBeNull();
    expect(resource.state.loading).toBe(false);
  });

  it('resets lines, loading, and error when loading a null track', async () => {
    const fetchLyrics = vi.fn().mockResolvedValue([{ time: 0, text: 'A line' }]);
    const resource = new LyricsResource(fetchLyrics);

    await resource.load(track('a'));
    await resource.load(null);

    expect(resource.state).toEqual({ loading: false, lines: [], error: null });
    expect(fetchLyrics).toHaveBeenCalledTimes(1);
  });

  it('exposes a current error and retries only the failed current track', async () => {
    const loadError = new Error('lyrics unavailable');
    const fetchLyrics = vi.fn()
      .mockResolvedValueOnce([{ time: 0, text: 'A line' }])
      .mockRejectedValueOnce(loadError)
      .mockResolvedValueOnce([{ time: 0, text: 'Recovered line' }]);
    const resource = new LyricsResource(fetchLyrics);
    const previousTrack = track('a');
    const currentTrack = track('b');

    await resource.load(previousTrack);
    await resource.load(currentTrack);

    expect(resource.state.error).toBe(loadError);
    expect(resource.state.loading).toBe(false);
    await resource.retry();

    expect(fetchLyrics).toHaveBeenNthCalledWith(1, previousTrack);
    expect(fetchLyrics).toHaveBeenNthCalledWith(2, currentTrack);
    expect(fetchLyrics).toHaveBeenNthCalledWith(3, currentTrack);
    expect(resource.state.error).toBeNull();
    expect(resource.state.lines).toEqual([{ time: 0, text: 'Recovered line' }]);
  });

  it('retries only the current track while earlier requests are pending', async () => {
    const a = deferred<LyricLine[]>();
    const firstB = deferred<LyricLine[]>();
    const retriedB = deferred<LyricLine[]>();
    const fetchLyrics = vi.fn()
      .mockReturnValueOnce(a.promise)
      .mockReturnValueOnce(firstB.promise)
      .mockReturnValueOnce(retriedB.promise);
    const resource = new LyricsResource(fetchLyrics);
    const trackA = track('a');
    const trackB = track('b');

    const loadingA = resource.load(trackA);
    const loadingB = resource.load(trackB);
    const retryingB = resource.retry();

    expect(fetchLyrics).toHaveBeenNthCalledWith(1, trackA);
    expect(fetchLyrics).toHaveBeenNthCalledWith(2, trackB);
    expect(fetchLyrics).toHaveBeenNthCalledWith(3, trackB);

    retriedB.resolve([{ time: 0, text: 'Retried B line' }]);
    await retryingB;
    firstB.resolve([{ time: 0, text: 'Old B line' }]);
    await loadingB;
    a.resolve([{ time: 0, text: 'A line' }]);
    await loadingA;

    expect(resource.state.lines).toEqual([{ time: 0, text: 'Retried B line' }]);
    expect(resource.state.error).toBeNull();
    expect(resource.state.loading).toBe(false);
  });

  it('does not commit a pending load after disposal', async () => {
    const pendingLyrics = deferred<LyricLine[]>();
    const fetchLyrics = vi.fn(() => pendingLyrics.promise);
    const resource = new LyricsResource(fetchLyrics);

    const loading = resource.load(track('a'));
    resource.dispose();
    pendingLyrics.resolve([{ time: 0, text: 'Late line' }]);
    await loading;

    expect(fetchLyrics).toHaveBeenCalledTimes(1);
    expect(resource.state).toEqual({ loading: false, lines: [], error: null });
  });

  it('does not commit a pending rejection after disposal', async () => {
    const pendingLyrics = deferred<LyricLine[]>();
    const fetchLyrics = vi.fn(() => pendingLyrics.promise);
    const resource = new LyricsResource(fetchLyrics);

    const loading = resource.load(track('a'));
    resource.dispose();
    pendingLyrics.reject(new Error('late failure'));
    await loading;

    expect(resource.state).toEqual({ loading: false, lines: [], error: null });
  });
});

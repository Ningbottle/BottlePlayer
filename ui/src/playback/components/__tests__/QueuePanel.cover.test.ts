import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';
import { playerStore } from '../../playerStore';

const mockFetchCoverImage = vi.fn();

vi.mock('../../data/coverGateway', () => ({
  fetchCoverImage: (...args: unknown[]) => mockFetchCoverImage(...args),
}));

// Import after mock so the component picks up the stub.
import QueuePanel from '../QueuePanel.vue';

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

function mkTrack(partial: Record<string, unknown>) {
  return {
    FileHash: 'hash-1',
    SongName: 'Song',
    SingerName: 'Artist',
    Duration: 100,
    Image: undefined as string | undefined,
    ...partial,
  } as any;
}

describe('QueuePanel cover fetch races', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    mockFetchCoverImage.mockReset();
    playerStore.queue = [];
    playerStore.currentTrack = null;
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('does not apply a stale cover after the track is removed from the queue', async () => {
    const pending = deferred<string>();
    // Always return the same deferred so deep-watch reentry still races the same response.
    mockFetchCoverImage.mockImplementation(() => pending.promise);

    const track = mkTrack({ FileHash: 'stale-hash', Image: undefined });
    playerStore.queue = [track];

    wrapper = mount(QueuePanel, { props: { show: true } });
    await Promise.resolve();

    expect(mockFetchCoverImage).toHaveBeenCalledWith('stale-hash');

    // Remove the track before the cover response arrives.
    playerStore.queue = [];
    await flushPromises();

    pending.resolve('https://cdn.example/cover-stale.jpg');
    await flushPromises();

    // Stale response must not write Image onto the detached track object.
    expect(track.Image).toBeFalsy();
    expect(playerStore.queue).toHaveLength(0);
  });

  it('does not re-enter cover fetch unboundedly once Image is filled (deep watch safe)', async () => {
    mockFetchCoverImage.mockResolvedValue('https://cdn.example/cover.jpg');

    const track = mkTrack({ FileHash: 'loop-hash', Image: undefined });
    playerStore.queue = [track];

    wrapper = mount(QueuePanel, { props: { show: true } });
    await flushPromises();
    // Extra ticks for deep-watch reentry if any.
    await flushPromises();
    await flushPromises();

    expect(track.Image).toBe('https://cdn.example/cover.jpg');
    // One fetch per missing hash — not a deep-watch storm from Image mutations.
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(1);
    expect(mockFetchCoverImage).toHaveBeenCalledWith('loop-hash');
  });

  it('ignores a stale cover after queue identity changes (remove then re-add same FileHash)', async () => {
    const first = deferred<string>();
    const second = deferred<string>();
    mockFetchCoverImage
      .mockImplementationOnce(() => first.promise)
      .mockImplementation(() => second.promise);

    const original = mkTrack({ FileHash: 'reuse-hash', Image: undefined });
    playerStore.queue = [original];

    wrapper = mount(QueuePanel, { props: { show: true } });
    await flushPromises();
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(1);

    // Empty the queue (identity change) then re-add a new object with the same hash.
    playerStore.queue = [];
    await flushPromises();
    const replacement = mkTrack({ FileHash: 'reuse-hash', Image: undefined });
    playerStore.queue = [replacement];
    await flushPromises();
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(2);

    // Stale first response must not write the detached original or the new track.
    first.resolve('https://cdn.example/old-cover.jpg');
    await flushPromises();
    expect(original.Image).toBeFalsy();
    expect(replacement.Image).toBeFalsy();

    // Current-generation response fills the live track only.
    second.resolve('https://cdn.example/new-cover.jpg');
    await flushPromises();
    expect(original.Image).toBeFalsy();
    expect(replacement.Image).toBe('https://cdn.example/new-cover.jpg');
  });

  it('stale cover completion does not clear pending for a newer in-flight fetch of the same hash', async () => {
    // Regression: .then always pendingCoverFetches.delete(hash) even when gen is stale,
    // which drops the marker for a concurrent re-fetch and can re-enter or skip ownership.
    const first = deferred<string>();
    const second = deferred<string>();
    mockFetchCoverImage
      .mockImplementationOnce(() => first.promise)
      .mockImplementation(() => second.promise);

    playerStore.queue = [mkTrack({ FileHash: 'same-hash', Image: undefined })];
    wrapper = mount(QueuePanel, { props: { show: true } });
    await flushPromises();
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(1);

    playerStore.queue = [];
    await flushPromises();
    playerStore.queue = [mkTrack({ FileHash: 'same-hash', Image: undefined })];
    await flushPromises();
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(2);

    // Stale first completes: must not drop pending so second remains the owner.
    first.resolve('https://cdn.example/old.jpg');
    await flushPromises();

    // Trigger another deep-watch tick while second still in flight — must NOT start a 3rd fetch.
    playerStore.queue = [...playerStore.queue];
    await flushPromises();
    expect(mockFetchCoverImage).toHaveBeenCalledTimes(2);

    second.resolve('https://cdn.example/new.jpg');
    await flushPromises();
    expect(playerStore.queue[0].Image).toBe('https://cdn.example/new.jpg');
  });
});

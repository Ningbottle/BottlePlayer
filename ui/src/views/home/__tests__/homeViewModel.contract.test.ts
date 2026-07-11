import { describe, expect, it, vi } from 'vitest';
import type { Track } from '../../../api/normalizer';

vi.mock('../../../api/playerStore', async () => {
  const { reactive } = await import('vue');

  return {
    playerStore: reactive({
      currentTrack: null as Track | null,
      isPlaying: false,
      queue: [] as Track[],
    }),
  };
});

vi.mock('../../../api/homeFeedStore', () => ({
  useHomeFeedStore: () => ({
    daily: { items: [] as Track[], loaded: true, loading: false, refreshing: false, error: null },
    playlists: { items: [], loaded: true, loading: false, refreshing: false, error: null },
    albums: { items: [], loaded: true, loading: false, refreshing: false, error: null },
  }),
}));

import { playerStore as playerStoreMock } from '../../../api/playerStore';
import { useHomeViewModel } from '../homeViewModel';

function makeTrack(FileHash: string): Track {
  return {
    FileHash,
    SongName: `Song ${FileHash}`,
    SingerName: 'Test Artist',
    Duration: 180,
  };
}

describe('useHomeViewModel', () => {
  it('exposes twelve queue rows, total count, active hash, and playback state', () => {
    playerStoreMock.currentTrack = makeTrack('hash-8');
    playerStoreMock.isPlaying = true;
    playerStoreMock.queue = Array.from({ length: 15 }, (_, index) => makeTrack(`hash-${index + 1}`));

    const model = useHomeViewModel().value;

    expect(model.queuePreview).toHaveLength(12);
    expect(model.queueTotal).toBe(15);
    expect(model.activeQueueHash).toBe('hash-8');
    expect(model.isPlaying).toBe(true);
  });
});

import { describe, expect, it, vi } from 'vitest';
import type { Track } from '../../../api/normalizer';

vi.mock('../../../api/playerStore', async () => {
  const { reactive } = await import('vue');

  return {
    playerStore: reactive({
      currentTrack: null as Track | null,
      currentIndex: -1,
      isPlaying: false,
      queue: [] as Track[],
      quality: '128',
      vipRequired: false,
    }),
  };
});

const homeFeedMock = {
  daily: { items: [] as Track[], loaded: true, loading: false, refreshing: false, error: null as string | null },
  playlists: { items: [] as any[], loaded: true, loading: false, refreshing: false, error: null as string | null },
  albums: { items: [] as any[], loaded: true, loading: false, refreshing: false, error: null as string | null },
};

vi.mock('../../../api/homeFeedStore', () => ({
  useHomeFeedStore: () => homeFeedMock,
}));

import { playerStore as playerStoreMock } from '../../../api/playerStore';
import {
  useHomeViewModel,
  formatHomeErrorSummary,
  buildHeroQualityChips,
} from '../homeViewModel';

function makeTrack(FileHash: string): Track {
  return {
    FileHash,
    SongName: `Song ${FileHash}`,
    SingerName: 'Test Artist',
    Duration: 180,
  };
}

describe('useHomeViewModel', () => {
  it('centers the twelve-row queue window around the active track', () => {
    playerStoreMock.currentTrack = makeTrack('hash-14');
    playerStoreMock.currentIndex = 13;
    playerStoreMock.isPlaying = true;
    playerStoreMock.queue = Array.from({ length: 20 }, (_, index) => makeTrack(`hash-${index + 1}`));

    const model = useHomeViewModel().value;

    expect(model.queuePreview).toHaveLength(12);
    expect(model.queuePreview.map((track) => track.FileHash)).toEqual([
      'hash-8', 'hash-9', 'hash-10', 'hash-11', 'hash-12', 'hash-13',
      'hash-14', 'hash-15', 'hash-16', 'hash-17', 'hash-18', 'hash-19',
    ]);
    expect(model.queueWindowStart).toBe(7);
    expect(model.queueTotal).toBe(20);
    expect(model.activeQueueHash).toBe('hash-14');
    expect(model.isPlaying).toBe(true);
  });

  it('builds a columnized error summary from failed sections', () => {
    homeFeedMock.daily.error = 'fail';
    homeFeedMock.playlists.error = 'fail';
    homeFeedMock.albums.error = null;
    playerStoreMock.currentTrack = null;
    playerStoreMock.queue = [];

    const model = useHomeViewModel().value;
    expect(model.errorSummary).toBe('每日推荐、编辑推荐加载失败');
    expect(model.errors).toHaveLength(2);
  });

  it('exposes hero quality chips only when hero is the current track', () => {
    const track = makeTrack('now');
    playerStoreMock.currentTrack = track;
    playerStoreMock.quality = 'flac';
    playerStoreMock.vipRequired = true;

    const model = useHomeViewModel().value;
    expect(model.heroQualityChips).toEqual(['无损', 'VIP']);
    expect(buildHeroQualityChips(makeTrack('other'), track, 'flac', false)).toEqual([]);
  });
});

describe('formatHomeErrorSummary', () => {
  it('joins section labels', () => {
    expect(formatHomeErrorSummary([
      { section: 'daily', message: 'x' },
      { section: 'albums', message: 'y' },
    ])).toBe('每日推荐、最新歌单加载失败');
  });
});

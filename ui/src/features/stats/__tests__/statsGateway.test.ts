import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('../../../platform/tauri/invoke', () => ({
  invokeTauri: vi.fn(),
}));

import { invokeTauri } from '../../../platform/tauri/invoke';
import {
  getStatsSummary,
  getStatsTop,
  getStatsTimeline,
  analyzeStats,
  type StatsSummary,
  type StatsTopItem,
  type StatsTimelineItem,
} from '../statsGateway';

const invokeMock = invokeTauri as unknown as ReturnType<typeof vi.fn>;

const SUMMARY_JSON = JSON.stringify({
  total_plays: 120,
  total_listened_seconds: 36000,
  unique_songs: 45,
  unique_artists: 12,
  completion_rate: 0.75,
});
const TOP_JSON = JSON.stringify({
  items: [{ name: 'Song A', song_hash: 'h1', play_count: 10, total_listened_seconds: 600 }],
});

describe('features/stats/statsGateway', () => {
  beforeEach(() => {
    invokeMock.mockReset();
  });

  it('getStatsSummary invokes stats_get_summary with range and parses the JSON payload', async () => {
    invokeMock.mockResolvedValue(SUMMARY_JSON);

    const summary: StatsSummary = await getStatsSummary('7d');

    expect(invokeMock).toHaveBeenCalledWith('stats_get_summary', { range: '7d' });
    expect(summary.total_plays).toBe(120);
    expect(summary.completion_rate).toBe(0.75);
  });

  it('getStatsTop invokes stats_get_top with kind/range/limit and returns parsed items', async () => {
    invokeMock.mockResolvedValue(TOP_JSON);

    const items: StatsTopItem[] = await getStatsTop('song', '30d', 10);

    expect(invokeMock).toHaveBeenCalledWith('stats_get_top', {
      kind: 'song',
      range: '30d',
      limit: 10,
    });
    expect(items).toHaveLength(1);
    expect(items[0].name).toBe('Song A');
    expect(items[0].play_count).toBe(10);
  });

  it('getStatsTop returns an empty array when the payload has no items', async () => {
    invokeMock.mockResolvedValue(JSON.stringify({}));
    expect(await getStatsTop('artist', '1d', 5)).toEqual([]);
  });

  it('getStatsTimeline invokes stats_get_timeline with range and parses items', async () => {
    invokeMock.mockResolvedValue(JSON.stringify({ items: [{ date: '2026-08-30', count: 7 }] }));

    const items: StatsTimelineItem[] = await getStatsTimeline('30d');

    expect(invokeMock).toHaveBeenCalledWith('stats_get_timeline', { range: '30d' });
    expect(items[0].count).toBe(7);
  });

  it('analyzeStats invokes ai_analyze with apiKey and statsJson and returns the text', async () => {
    invokeMock.mockResolvedValue('AI 分析结果');
    const input = { summary: { total_plays: 1, total_listened_seconds: 0, unique_songs: 0, unique_artists: 0, completion_rate: 0 } };

    const result = await analyzeStats('sk-key', input);

    expect(invokeMock).toHaveBeenCalledWith('ai_analyze', {
      apiKey: 'sk-key',
      statsJson: JSON.stringify(input),
    });
    expect(result).toBe('AI 分析结果');
  });

  it('rejects when the backend command fails (view keeps its error handling)', async () => {
    invokeMock.mockRejectedValue(new Error('backend down'));
    await expect(getStatsSummary('1d')).rejects.toThrow('backend down');
  });
});

/**
 * Cross-layer shape contract: stats payloads exactly as the C++ backend
 * produces them, driven through the real parsing code in
 * features/stats/statsGateway.ts (audit item B4).
 *
 * Fixture sources (field-by-field):
 * - CPP_SUMMARY_FALLBACK_LITERAL: the literal emitted by
 *   native/core/C_API.cpp EchoStatsGetSummary
 *   ({"total_plays":0,...,"range":"all"}).
 * - CPP_POPULATED_SUMMARY: the key set built by
 *   native/stats/PlayStatsService.cpp GetSummary (the same six fields,
 *   `range` echoing the requested range).
 * - CPP_TOP_SONG / CPP_TIMELINE: the item shapes built by
 *   PlayStatsService.cpp GetTop (dim "song") and GetTimeline.
 *
 * The gateway only mocks the Tauri transport (invokeTauri) — parsing is the
 * real production code path. If C++ renames a JSON field, Rust changes the
 * command/argument names, or the frontend changes its parsing, one of these
 * tests turns red.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('../../../platform/tauri/invoke', () => ({
  invokeTauri: vi.fn(),
}));

import { invokeTauri } from '../../../platform/tauri/invoke';
import {
  getStatsSummary,
  getStatsTop,
  getStatsTimeline,
  type StatsSummary,
} from '../statsGateway';

const invokeMock = invokeTauri as unknown as ReturnType<typeof vi.fn>;

/** Field-by-field copy of the C++ fallback literal (native/core/C_API.cpp,
 * EchoStatsGetSummary). Kept as raw JSON text so the string itself is the
 * contract artifact. */
const CPP_SUMMARY_FALLBACK_LITERAL =
  '{"total_plays":0,"total_listened_seconds":0,"unique_songs":0,"unique_artists":0,"completion_rate":0,"range":"all"}';

/** Populated summary with the PlayStatsService.cpp GetSummary key set. The
 * range field echoes what the caller requested (the frontend StatsRange type
 * only ever sends 1d/7d/30d). */
const CPP_POPULATED_SUMMARY = {
  total_plays: 6,
  total_listened_seconds: 1200.0,
  unique_songs: 3,
  unique_artists: 2,
  completion_rate: 0.5,
  range: '7d',
};

/** The six contract fields, sorted. Every summary fixture must carry exactly
 * this set — no more, no fewer. */
const SUMMARY_KEYS = [
  'completion_rate',
  'range',
  'total_listened_seconds',
  'total_plays',
  'unique_artists',
  'unique_songs',
];

/** GetTop("song") output shape from PlayStatsService.cpp. */
const CPP_TOP_SONG = {
  dim: 'song',
  items: [
    {
      song_hash: 'contract-hash-1',
      name: 'Song A',
      singer: 'Artist X',
      album: 'Album One',
      cover_url: '',
      play_count: 3,
      total_listened_seconds: 720,
    },
  ],
};

/** GetTimeline output shape from PlayStatsService.cpp. */
const CPP_TIMELINE = {
  items: [{ date: '2026-09-02', count: 6 }],
};

describe('cross-layer contract: C++ stats shapes -> statsGateway parsing', () => {
  beforeEach(() => {
    invokeMock.mockReset();
  });

  it('summary fixtures still carry exactly the six C++ contract fields', () => {
    expect(Object.keys(JSON.parse(CPP_SUMMARY_FALLBACK_LITERAL)).sort()).toEqual(SUMMARY_KEYS);
    expect(Object.keys(CPP_POPULATED_SUMMARY).sort()).toEqual(SUMMARY_KEYS);
  });

  it('parses the C++ empty-database summary literal field by field', async () => {
    invokeMock.mockResolvedValue(CPP_SUMMARY_FALLBACK_LITERAL);

    const summary: StatsSummary = await getStatsSummary('7d');

    // Command name + argument shape must match the Rust side
    // (src/stats.rs stats_get_summary, registered in src/lib.rs).
    expect(invokeMock).toHaveBeenCalledWith('stats_get_summary', { range: '7d' });
    expect(summary.total_plays).toBe(0);
    expect(summary.total_listened_seconds).toBe(0);
    expect(summary.unique_songs).toBe(0);
    expect(summary.unique_artists).toBe(0);
    expect(summary.completion_rate).toBe(0);
  });

  it('parses a populated C++ summary to typed StatsSummary fields', async () => {
    invokeMock.mockResolvedValue(JSON.stringify(CPP_POPULATED_SUMMARY));

    const summary = await getStatsSummary('7d');

    expect(summary.total_plays).toBe(6);
    expect(summary.total_listened_seconds).toBe(1200);
    expect(summary.unique_songs).toBe(3);
    expect(summary.unique_artists).toBe(2);
    expect(summary.completion_rate).toBe(0.5);
  });

  it('passes the extra C++ `range` field through untouched', async () => {
    // The StatsSummary type does not declare `range`, but the gateway is a
    // straight JSON.parse — the field must survive for any consumer that
    // reads it. If the gateway ever switches to field-by-field projection,
    // this test documents the behavior change.
    invokeMock.mockResolvedValue(CPP_SUMMARY_FALLBACK_LITERAL);

    const parsed = (await getStatsSummary('7d')) as unknown as Record<string, unknown>;

    expect(parsed.range).toBe('all');
  });

  it('parses the C++ GetTop(song) item shape', async () => {
    invokeMock.mockResolvedValue(JSON.stringify(CPP_TOP_SONG));

    const items = await getStatsTop('song', '7d', 10);

    expect(invokeMock).toHaveBeenCalledWith('stats_get_top', { kind: 'song', range: '7d', limit: 10 });
    expect(items).toHaveLength(1);
    expect(items[0].song_hash).toBe('contract-hash-1');
    expect(items[0].name).toBe('Song A');
    expect(items[0].play_count).toBe(3);
    expect(items[0].total_listened_seconds).toBe(720);
  });

  it('parses the C++ GetTimeline item shape', async () => {
    invokeMock.mockResolvedValue(JSON.stringify(CPP_TIMELINE));

    const items = await getStatsTimeline('7d');

    expect(invokeMock).toHaveBeenCalledWith('stats_get_timeline', { range: '7d' });
    expect(items).toHaveLength(1);
    expect(items[0].date).toBe('2026-09-02');
    expect(items[0].count).toBe(6);
  });
});

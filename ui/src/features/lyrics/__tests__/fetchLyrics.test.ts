import { beforeEach, describe, expect, it, vi } from 'vitest';

const searchLyricCandidates = vi.fn();
const fetchLyricDetail = vi.fn();

vi.mock('../lyricsGateway', () => ({
  searchLyricCandidates: (...args: unknown[]) => searchLyricCandidates(...args),
  fetchLyricDetail: (...args: unknown[]) => fetchLyricDetail(...args),
}));

import { fetchLyrics, LyricLoadError } from '../useLyricStage';
import type { Track } from '../../../shared/music/track';

const track = { FileHash: 'abc123def456', SongName: 'Test', SingerName: 'A', Duration: 100 } as Track;

describe('fetchLyrics error codes', () => {
  beforeEach(() => {
    searchLyricCandidates.mockReset();
    fetchLyricDetail.mockReset();
  });

  it('keeps the native search error code instead of collapsing to a generic message', async () => {
    searchLyricCandidates.mockResolvedValue({
      status: 0,
      error_code: 'native_lyric_search_failed',
      error: 'timeout',
      diagnostics: { timed_out: true, upstream_http_status: 0 },
    });

    await expect(fetchLyrics(track)).rejects.toMatchObject({
      name: 'LyricLoadError',
      phase: 'search',
      code: 'native_lyric_search_failed',
    });
    await expect(fetchLyrics(track)).rejects.toThrow(/native_lyric_search_failed/);
    expect(searchLyricCandidates).toHaveBeenCalledWith('abc123def456');
    expect(fetchLyricDetail).not.toHaveBeenCalled();
  });

  it('keeps an upstream business rejection code instead of collapsing to search_failed', async () => {
    searchLyricCandidates.mockResolvedValue({
      status: 0,
      error_code: 20007,
      error: '',
    });

    await expect(fetchLyrics(track)).rejects.toMatchObject({
      name: 'LyricLoadError',
      phase: 'search',
      code: '20007',
    });
    await expect(fetchLyrics(track)).rejects.toThrow(/20007/);
    expect(fetchLyricDetail).not.toHaveBeenCalled();
  });

  it('keeps the download error code when search succeeded', async () => {
    searchLyricCandidates.mockResolvedValue({
      status: 200,
      candidates: [{ id: '1', accesskey: 'k' }],
    });
    fetchLyricDetail.mockResolvedValue({
      status: 0,
      error_code: 'native_lyric_download_failed',
      error: 'WINHTTP_TIMEOUT',
    });

    await expect(fetchLyrics(track)).rejects.toBeInstanceOf(LyricLoadError);
    await expect(fetchLyrics(track)).rejects.toMatchObject({
      phase: 'download',
      code: 'native_lyric_download_failed',
    });
  });
});

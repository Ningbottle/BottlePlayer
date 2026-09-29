import { describe, it, expect, vi, beforeEach } from 'vitest';

const mockApiGet = vi.fn();

vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { resolveTrack, probeSongUrl } from '../songUrlGateway';
import type { Track } from '../../../shared/music/track';

describe('playback/data/songUrlGateway contract', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('resolveTrack calls /song/url with track parameters and quality', async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, url: 'http://test/stream.flac' });
    const track: Track = {
      FileHash: 'HASH123',
      AlbumID: 'ALBUM456',
      AlbumAudioID: 'AUDIO789',
      SongName: 'Song',
      ArtistName: 'Artist',
      SingerName: 'Artist',
      Duration: 180,
    };
    const res = await resolveTrack(track, 'lossless');
    expect(mockApiGet).toHaveBeenCalledWith('/song/url', {
      hash: 'HASH123',
      album_id: 'ALBUM456',
      album_audio_id: 'AUDIO789',
      quality: 'lossless',
    });
    expect(res.url).toBe('http://test/stream.flac');
  });

  it('probeSongUrl calls /song/url with probe parameters', async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, url: 'http://test/full/audio' });
    const res = await probeSongUrl({
      hash: 'F0A6BA24635A8560F96C2C2D603E8CA8',
      album_id: '1776319',
      album_audio_id: '39905465',
    });
    expect(mockApiGet).toHaveBeenCalledWith('/song/url', {
      hash: 'F0A6BA24635A8560F96C2C2D603E8CA8',
      album_id: '1776319',
      album_audio_id: '39905465',
    });
    expect(res.url).toBe('http://test/full/audio');
  });

  it('adapts native per-quality snake_case flags and preserves delivery', async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, delivery: 'full', data: {
      available_qualities: [
        { quality: '320', url: 'https://cdn/full', is_preview: false, delivery: 'full' },
        { quality: '128', url: 'https://cdn/clip', is_preview: true, delivery: 'preview' },
        { quality: 'flac', url: 'https://cdn/opaque', is_preview: false, delivery: 'unknown' },
      ],
    } });
    const result = await resolveTrack({ FileHash: 'hash' } as Track, '128');
    expect(result.data?.available_qualities).toEqual([
      expect.objectContaining({ quality: '320', isPreview: false, delivery: 'full' }),
      expect.objectContaining({ quality: '128', isPreview: true, delivery: 'preview' }),
      expect.objectContaining({ quality: 'flac', isPreview: false, delivery: 'unknown' }),
    ]);
  });

  it('preserves the rejection chain while adapting the selected playback result', async () => {
    const attempts = [
      { endpoint: 'v5-main', http_status: 200, errcode: 20018, fail_process: ['pkg', 'buy'],
        anonymous: false, quality: '128', delivery: 'unknown', is_preview: false, error: '' },
      { endpoint: 'v5-preview', http_status: 200, errcode: 0, fail_process: null,
        anonymous: true, quality: '128', delivery: 'preview', is_preview: true, error: '' },
    ];
    mockApiGet.mockResolvedValueOnce({ status: 1, url: 'https://cdn/clip', delivery: 'preview',
      is_preview: true, attempts, data: { attempts } });
    const result = await resolveTrack({ FileHash: 'hash' } as Track, '128');
    expect(result.attempts).toEqual(attempts);
    expect(result.data?.attempts).toEqual(attempts);
    expect(result.is_preview).toBe(true);
  });
});

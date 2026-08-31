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
});

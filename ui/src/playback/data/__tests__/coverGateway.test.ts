import { describe, it, expect, vi, beforeEach } from 'vitest';
import { fetchCoverImage } from '../../data/coverGateway';

vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: vi.fn(),
}));

import { apiGet } from '../../../platform/tauri/nativeClient';

const apiGetMock = apiGet as unknown as ReturnType<typeof vi.fn>;

describe('playback/data/coverGateway: fetchCoverImage', () => {
  beforeEach(() => {
    apiGetMock.mockReset();
  });

  it('returns "" and skips apiGet when hash is empty', async () => {
    const result = await fetchCoverImage('');
    expect(result).toBe('');
    expect(apiGetMock).not.toHaveBeenCalled();
  });

  it('requests /images/audio with hash and album_audio_id args', async () => {
    apiGetMock.mockResolvedValue({ status: 0 });
    await fetchCoverImage('HASH1', 'ALBUM7');
    expect(apiGetMock).toHaveBeenCalledWith('/images/audio', {
      hash: 'HASH1',
      album_audio_id: 'ALBUM7',
    });
  });

  it('unwraps res.data arrays and nested first rows', async () => {
    apiGetMock.mockResolvedValue({
      status: 1,
      data: [[{ sizable_portrait: 'https://x/{size}/p' }]],
    });
    expect(await fetchCoverImage('H')).toBe('https://x/400/p');

    apiGetMock.mockResolvedValue({
      status: 1,
      data: [{ sizable_portrait: 'https://x/{size}/q' }],
    });
    expect(await fetchCoverImage('H')).toBe('https://x/400/q');

    apiGetMock.mockResolvedValue({
      status: 1,
      data: { sizable_portrait: 'https://x/{size}/r' },
    });
    expect(await fetchCoverImage('H')).toBe('https://x/400/r');
  });

  it('prefers sizable_portrait then sizable_avatar', async () => {
    apiGetMock.mockResolvedValue({
      status: 1,
      data: { sizable_portrait: 'P', sizable_avatar: 'A' },
    });
    expect(await fetchCoverImage('H')).toBe('P');

    apiGetMock.mockResolvedValue({ status: 1, data: { sizable_avatar: 'A' } });
    expect(await fetchCoverImage('H')).toBe('A');
  });

  it('picks the largest numeric key inside imgs', async () => {
    apiGetMock.mockResolvedValue({
      status: 1,
      data: {
        imgs: {
          100: [{ sizable_portrait: 'small' }],
          400: [{ sizable_portrait: 'big/{size}/x' }],
          200: [{ sizable_portrait: 'mid' }],
        },
      },
    });
    expect(await fetchCoverImage('H')).toBe('big/400/x');
  });

  it('returns "" for status !== 1, empty data, or empty item', async () => {
    apiGetMock.mockResolvedValue({ status: 0, data: { sizable_portrait: 'x' } });
    expect(await fetchCoverImage('H')).toBe('');

    apiGetMock.mockResolvedValue({ status: 1, data: null });
    expect(await fetchCoverImage('H')).toBe('');

    apiGetMock.mockResolvedValue({ status: 1, data: {} });
    expect(await fetchCoverImage('H')).toBe('');
  });

  it('returns "" and logs best-effort when apiGet rejects', async () => {
    const errSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    apiGetMock.mockRejectedValue(new Error('offline'));
    expect(await fetchCoverImage('H')).toBe('');
    expect(errSpy).toHaveBeenCalled();
    errSpy.mockRestore();
  });
});

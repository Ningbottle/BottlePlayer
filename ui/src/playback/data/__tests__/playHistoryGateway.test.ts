import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiPost: vi.fn(),
}));

import { apiPost } from '../../../platform/tauri/nativeClient';
import {
  uploadPlayHistory,
  configurePlayHistoryPolicy,
  __resetPlayHistoryPolicyForTests,
} from '../playHistoryGateway';
import type { Track } from '../../../shared/music/track';

const apiPostMock = apiPost as unknown as ReturnType<typeof vi.fn>;

function mkTrack(overrides: Partial<Track> = {}): Track {
  return {
    FileHash: 'hash-1',
    SongName: 'Song',
    SingerName: 'Artist',
    Duration: 200,
    ...overrides,
  } as Track;
}

describe('playHistoryGateway upload policy', () => {
  beforeEach(() => {
    apiPostMock.mockReset();
    __resetPlayHistoryPolicyForTests();
  });

  it('does not upload when no policy is configured (default disabled)', async () => {
    await uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }));
    expect(apiPostMock).not.toHaveBeenCalled();
  });

  it('does not upload when policy.isUploadEnabled() is false', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => false });
    await uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }));
    expect(apiPostMock).not.toHaveBeenCalled();
  });

  it('uploads to /playhistory/upload when policy allows and mxid is valid', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    await uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }));
    expect(apiPostMock).toHaveBeenCalledTimes(1);
    const [path, , args] = apiPostMock.mock.calls[0];
    expect(path).toBe('/playhistory/upload');
    expect(args.mxid).toBe(777);
    expect(args.pc).toBe(1);
    expect(typeof args.time).toBe('number');
  });

  it('keeps the mxid/time/pc argument shape (no field changes)', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    await uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }));
    expect(Object.keys(apiPostMock.mock.calls[0][2]).sort()).toEqual(['mxid', 'pc', 'time']);
  });

  it('does not upload when mxid is missing or invalid', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    await uploadPlayHistory(mkTrack({ AlbumAudioID: undefined, MixSongID: undefined }));
    expect(apiPostMock).not.toHaveBeenCalled();

    await uploadPlayHistory(mkTrack({ AlbumAudioID: '-3' }));
    expect(apiPostMock).not.toHaveBeenCalled();

    await uploadPlayHistory(mkTrack({ AlbumAudioID: '0' }));
    expect(apiPostMock).not.toHaveBeenCalled();
  });

  it('stays silent on API rejection (playback unaffected)', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    apiPostMock.mockRejectedValue(new Error('network down'));
    await expect(uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }))).resolves.toBeUndefined();
    expect(warnSpy).toHaveBeenCalled();
    warnSpy.mockRestore();
  });

  it('falls back to MixSongID when AlbumAudioID is absent', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    await uploadPlayHistory(mkTrack({ AlbumAudioID: undefined, MixSongID: '555' }));
    expect(apiPostMock).toHaveBeenCalledTimes(1);
    expect(apiPostMock.mock.calls[0][2].mxid).toBe(555);
  });

  it('restores the default-disabled policy after reset', async () => {
    configurePlayHistoryPolicy({ isUploadEnabled: () => true });
    __resetPlayHistoryPolicyForTests();
    await uploadPlayHistory(mkTrack({ AlbumAudioID: '777' }));
    expect(apiPostMock).not.toHaveBeenCalled();
  });
});

import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Track } from '../../../shared/music/track';

const account = vi.hoisted(() => ({ isLoggedIn: true }));
const apiPost = vi.hoisted(() => vi.fn());
vi.mock('../../account', () => ({ userStore: account }));
vi.mock('../../../platform/tauri/nativeClient', () => ({ apiPost }));

import { addTrackToPlaylist } from '../favorite';

const playlist = { id: 'collection_3_42_98765_0', listid: '98765', name: '我的歌单' };
const track = (values: Partial<Track> = {}): Track => ({
  FileHash: 'HASH1', SongName: '', SingerName: '', Duration: 0, ...values,
});

// The structured protocol must carry a name verbatim: commas, a literal pipe, a
// literal "%7C" and non-ASCII are exactly what the legacy pipe/comma encoding
// could not express.
const structuredNames = [
  { name: 'Song, with comma | pipe %7C and 中文', hash: 'HASH1', album_id: 111, mixsongid: 222 },
  { name: '', hash: 'HASH2', album_id: 0, mixsongid: 0 },
  { name: '100% Done', hash: 'HASH3', album_id: 333, mixsongid: 444 },
];

describe('favorite adapter structured native contract', () => {
  beforeEach(() => {
    apiPost.mockReset().mockResolvedValue({ status: 1 });
    account.isLoggedIn = true;
  });

  it.each(structuredNames)('preserves the name verbatim: $name', async (entry) => {
    await expect(addTrackToPlaylist(playlist, track({
      FileHash: entry.hash, SongName: entry.name,
      AlbumID: String(entry.album_id), AlbumAudioID: String(entry.mixsongid),
    }))).resolves.toEqual({ success: true });

    expect(apiPost).toHaveBeenCalledTimes(1);
    expect(apiPost.mock.calls[0][0]).toBe('/playlist/tracks/add');
    const payload = JSON.parse(apiPost.mock.calls[0][1]);
    expect(payload).toEqual({ listid: '98765', data: [{
      ...entry, album_id: String(entry.album_id), mixsongid: String(entry.mixsongid),
    }] });
    expect(apiPost.mock.calls[0][2]).toBeUndefined();
  });

  it('defaults optional ids without inventing a song name', async () => {
    await addTrackToPlaylist(playlist, track());
    expect(JSON.parse(apiPost.mock.calls[0][1]).data).toEqual([
      { name: '', hash: 'HASH1', album_id: 0, mixsongid: 0 },
    ]);
  });

  it('does not round large numeric string identities', async () => {
    await addTrackToPlaylist(playlist, track({ AlbumID: '9007199254740993' }));
    expect(JSON.parse(apiPost.mock.calls[0][1]).data[0].album_id).toBe('9007199254740993');
  });

  it.each(['', '   '])('rejects an absent hash before IPC: %j', async (FileHash) => {
    expect(await addTrackToPlaylist(playlist, track({ FileHash }))).toEqual({
      success: false, error: '缺少歌曲标识，无法收藏',
    });
    expect(apiPost).not.toHaveBeenCalled();
  });

  it('keeps transport failures available to the existing outbox', async () => {
    apiPost.mockRejectedValueOnce(new Error('request_timeout'));
    await expect(addTrackToPlaylist(playlist, track())).rejects.toThrow('request_timeout');
    expect(apiPost).toHaveBeenCalledTimes(1);
  });

  it('does not send a request when logged out', async () => {
    account.isLoggedIn = false;
    expect((await addTrackToPlaylist(playlist, track())).success).toBe(false);
    expect(apiPost).not.toHaveBeenCalled();
  });
});

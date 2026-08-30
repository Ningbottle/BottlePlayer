import { describe, it, expect } from 'vitest';
import { normalizeTrack } from '../../music/track';

describe('shared/music/track: normalizeTrack', () => {
  it('null/undefined input returns unknown-song defaults', () => {
    for (const raw of [null, undefined]) {
      const t = normalizeTrack(raw);
      expect(t.FileHash).toBe('');
      expect(t.SongName).toBe('未知歌曲');
      expect(t.SingerName).toBe('未知歌手');
      expect(t.Duration).toBe(0);
    }
  });

  it('accepts canonical field names', () => {
    const t = normalizeTrack({
      FileHash: 'abc123',
      SongName: 'Song',
      SingerName: 'Artist',
      AlbumName: 'Album',
      AlbumID: 'a1',
      AlbumAudioID: 'aa1',
      Duration: 180,
    });
    expect(t.FileHash).toBe('abc123');
    expect(t.SongName).toBe('Song');
    expect(t.SingerName).toBe('Artist');
    expect(t.AlbumName).toBe('Album');
    expect(t.AlbumID).toBe('a1');
    expect(t.AlbumAudioID).toBe('aa1');
    expect(t.Duration).toBe(180);
  });

  it('maps lowercase/hash aliases (hash, Filehash, songname, filename, singername, author_name)', () => {
    const t = normalizeTrack({
      hash: 'h1',
      Filehash: 'h1',
      songname: 'S',
      filename: 'S.mp3',
      singername: 'A',
      author_name: 'A',
    });
    expect(t.FileHash).toBe('h1');
    expect(t.SongName).toBe('S');
    expect(t.SingerName).toBe('A');
  });

  it('maps album aliases (album_name, albumname, album_id, albumid)', () => {
    const t = normalizeTrack({ album_name: 'AN', album_id: 'aid' });
    expect(t.AlbumName).toBe('AN');
    expect(t.AlbumID).toBe('aid');
  });

  it('maps AlbumAudioID aliases (album_audio_id, mixsongid, MixSongID)', () => {
    expect(normalizeTrack({ album_audio_id: 7 }).AlbumAudioID).toBe(7);
    expect(normalizeTrack({ mixsongid: 8 }).AlbumAudioID).toBe(8);
    expect(normalizeTrack({ MixSongID: 9 }).AlbumAudioID).toBe(9);
  });

  it('treats duration / time_length as seconds', () => {
    expect(normalizeTrack({ duration: 200 }).Duration).toBe(200);
    expect(normalizeTrack({ time_length: 200 }).Duration).toBe(200);
  });

  it('converts timelen from milliseconds to seconds', () => {
    expect(normalizeTrack({ timelen: 200000 }).Duration).toBe(200);
  });

  it('rescues mislabeled millisecond values above 18000', () => {
    expect(normalizeTrack({ duration: 200000 }).Duration).toBe(200);
  });

  it('keeps the existing cover field priority', () => {
    expect(normalizeTrack({ Image: 'A', imgurl: 'B' }).Image).toBe('A');
    expect(normalizeTrack({ imgurl: 'B', img: 'C' }).Image).toBe('B');
    expect(normalizeTrack({ img: 'C', cover: 'D' }).Image).toBe('C');
    expect(normalizeTrack({ cover: 'D', pic_url: 'E' }).Image).toBe('D');
    expect(normalizeTrack({ pic_url: 'E', sizable_cover: 'F' }).Image).toBe('E');
    expect(normalizeTrack({ sizable_cover: 'F', album_sizable_cover: 'G' }).Image).toBe('F');
    expect(normalizeTrack({ album_sizable_cover: 'G', album_cover: 'H' }).Image).toBe('G');
    expect(normalizeTrack({ album_cover: 'H', album_imgurl: 'I' }).Image).toBe('H');
  });

  it('extracts cover from nested union_cover / albuminfo / singerinfo paths', () => {
    expect(normalizeTrack({ trans_param: { union_cover: 'U' } }).Image).toBe('U');
    expect(normalizeTrack({ albuminfo: { sizable_cover: 'S' } }).Image).toBe('S');
    expect(normalizeTrack({ albuminfo: { imgurl: 'I' } }).Image).toBe('I');
    expect(normalizeTrack({ singerinfo: [{ avatar: 'V' }] }).Image).toBe('V');
  });

  it('replaces the {size} placeholder with 400', () => {
    expect(normalizeTrack({ imgurl: 'https://x/{size}/img' }).Image).toBe('https://x/400/img');
  });

  it('leaves Image undefined when no picture fields exist', () => {
    expect(normalizeTrack({ SongName: 'x' }).Image).toBeUndefined();
  });

  it('preserves the other raw fields on the result', () => {
    const t = normalizeTrack({ FileHash: 'h', Price: 100, privilege: 8 });
    expect(t.Price).toBe(100);
    expect(t.privilege).toBe(8);
  });
});

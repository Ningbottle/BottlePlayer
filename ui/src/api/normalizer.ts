import { apiGet } from './backend';

export interface Track {
  FileHash: string;
  SongName: string;
  SingerName: string;
  AlbumName?: string;
  AlbumID?: string;
  AlbumAudioID?: string;
  Duration: number;
  Image?: string;
  [key: string]: any;
}

export function normalizeTrack(raw: any): Track {
  if (!raw) {
    return {
      FileHash: '',
      SongName: '未知歌曲',
      SingerName: '未知歌手',
      Duration: 0,
    };
  }

  const FileHash = raw.FileHash || raw.hash || raw.Filehash || '';
  const SongName = raw.SongName || raw.songname || raw.filename || '未知歌曲';
  const SingerName = raw.SingerName || raw.singername || raw.author_name || '未知歌手';
  const AlbumName = raw.AlbumName || raw.album_name || raw.albumname || undefined;
  const AlbumID = raw.AlbumID || raw.album_id || raw.albumid || undefined;
  const AlbumAudioID = raw.AlbumAudioID || raw.album_audio_id || raw.mixsongid || raw.MixSongID || undefined;
  const Duration = Number(raw.Duration || raw.duration || raw.time_length || 0);

  // Cover image extraction — KuGou's many endpoints stash the cover URL in
  // different field names. Walk through them in order of preference.
  // `union_cover` and `album_sizable_cover` typically come from /top/song,
  // /everyday/recommend, and /search responses.
  let Image: string | undefined =
      raw.Image ||
      raw.imgurl ||
      raw.img ||
      raw.cover ||
      raw.pic_url ||
      raw.sizable_cover ||
      raw.album_sizable_cover ||
      raw.album_cover ||
      raw.album_imgurl ||
      raw.trans_param?.union_cover ||
      raw.albuminfo?.sizable_cover ||
      raw.albuminfo?.imgurl ||
      raw.singerinfo?.[0]?.avatar ||
      undefined;
  // Most KuGou cover URLs embed a `{size}` placeholder (e.g. /400/ for 400px).
  if (Image && typeof Image === 'string') {
    Image = Image.replace('{size}', '400');
  }

  return {
    ...raw,
    FileHash,
    SongName,
    SingerName,
    AlbumName,
    AlbumID,
    AlbumAudioID,
    Duration,
    Image,
  };
}

export async function fetchCoverImage(hash: string, albumAudioId: string = ''): Promise<string> {
  if (!hash) return '';
  try {
    const res = await apiGet<any>('/images/audio', {
      hash: hash,
      album_audio_id: albumAudioId,
    });
    if (res && res.status === 1 && res.data) {
      let item = null;
      if (Array.isArray(res.data) && res.data[0]) {
        item = Array.isArray(res.data[0]) ? res.data[0][0] : res.data[0];
      } else if (!Array.isArray(res.data)) {
        item = res.data;
      }
      
      if (item) {
        if (item.sizable_portrait) return item.sizable_portrait.replace('{size}', '400');
        if (item.sizable_avatar) return item.sizable_avatar.replace('{size}', '400');
        if (item.imgs) {
          const keys = Object.keys(item.imgs).sort((a, b) => Number(b) - Number(a));
          if (keys.length > 0) {
            const imgArr = item.imgs[keys[0]];
            if (imgArr && imgArr.length > 0 && imgArr[0].sizable_portrait) {
              return imgArr[0].sizable_portrait.replace('{size}', '400');
            }
          }
        }
      }
    }
  } catch (e) {
    console.error('Failed to fetch cover image for hash', hash, e);
  }
  return '';
}

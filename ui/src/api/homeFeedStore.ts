import { reactive } from 'vue';
import { apiGet } from './backend';
import { normalizeTrack, type Track } from './normalizer';

export interface PlaylistInfo {
  specialid: number;
  specialname: string;
  imgurl: string;
  nickname: string;
  playcount: number;
  [key: string]: unknown;
}

export interface HomeSectionState<T> {
  items: T[];
  loading: boolean;
  refreshing: boolean;
  error: string | null;
  loaded: boolean;
}

type HomeSection = 'daily' | 'playlists' | 'albums';

function createSectionState<T>(): HomeSectionState<T> {
  return {
    items: [],
    loading: false,
    refreshing: false,
    error: null,
    loaded: false,
  };
}

const daily = reactive<HomeSectionState<Track>>(createSectionState());
const playlists = reactive<HomeSectionState<PlaylistInfo>>(createSectionState());
const albums = reactive<HomeSectionState<PlaylistInfo>>(createSectionState());

let inFlight: Promise<void> | null = null;

function beginLoad<T>(section: HomeSectionState<T>, refreshing: boolean) {
  section.error = null;
  if (refreshing) {
    section.refreshing = true;
  } else {
    section.loading = true;
  }
}

function finishLoad<T>(section: HomeSectionState<T>, refreshing: boolean) {
  section.loaded = true;
  if (refreshing) {
    section.refreshing = false;
  } else {
    section.loading = false;
  }
}

function normalizePlaylist(playlist: Record<string, unknown>): PlaylistInfo {
  const imgurl = typeof playlist.imgurl === 'string'
    ? playlist.imgurl.replace('{size}', '400')
    : typeof playlist.pic_url === 'string'
      ? playlist.pic_url.replace('{size}', '400')
      : '';

  return { ...playlist, imgurl } as PlaylistInfo;
}

async function loadDaily(refreshing: boolean) {
  beginLoad(daily, refreshing);
  try {
    const songRes = await apiGet<any>('/everyday/recommend', { pagesize: 6 });
    const songData = songRes.data?.data || songRes.data || {};
    const songList = songData.song_list || songData.info || songData.list;
    if (songRes.status === 1 && songList && songList.length > 0) {
      daily.items = songList.slice(0, 6).map(normalizeTrack);
    } else {
      const fallbackRes = await apiGet<any>('/top/song', { pagesize: 6 });
      const fallbackData = fallbackRes.data?.data || fallbackRes.data || {};
      const fallbackList = fallbackData.info || fallbackData.list;
      if (fallbackRes.status === 1 && fallbackList) {
        daily.items = fallbackList.slice(0, 6).map(normalizeTrack);
      }
    }
  } catch (error) {
    daily.error = '加载失败';
    console.error('Failed to load daily recommendations', error);
  } finally {
    finishLoad(daily, refreshing);
  }
}

async function loadPlaylistSection(
  section: HomeSectionState<PlaylistInfo>,
  sort: number,
  errorMessage: string,
  refreshing: boolean,
) {
  beginLoad(section, refreshing);
  try {
    const response = await apiGet<any>('/top/playlist', { pagesize: 5, sort });
    const data = response.data?.data || response.data || {};
    const list = data.info || data.list;
    if (response.status === 1 && list) {
      section.items = list.slice(0, 5).map(normalizePlaylist);
    }
  } catch (error) {
    section.error = '加载失败';
    console.error(errorMessage, error);
  } finally {
    finishLoad(section, refreshing);
  }
}

function loadSections(sections: HomeSection[], refreshing: boolean): Promise<void> {
  return Promise.all(sections.map((section) => {
    if (section === 'daily') return loadDaily(refreshing);
    if (section === 'playlists') {
      return loadPlaylistSection(playlists, 2, 'Failed to load recommended playlists', refreshing);
    }
    return loadPlaylistSection(albums, 5, 'Failed to load new albums', refreshing);
  })).then(() => undefined);
}

function startLoad(sections: HomeSection[], refreshing: boolean): Promise<void> {
  inFlight = loadSections(sections, refreshing).finally(() => {
    inFlight = null;
  });
  return inFlight;
}

function ensureLoaded(): Promise<void> {
  if (inFlight) return inFlight;

  const unloaded = (['daily', 'playlists', 'albums'] as HomeSection[])
    .filter((section) => !({ daily, playlists, albums }[section].loaded));
  return unloaded.length > 0 ? startLoad(unloaded, false) : Promise.resolve();
}

function refresh(): Promise<void> {
  if (inFlight) return inFlight;
  return startLoad(['daily', 'playlists', 'albums'], true);
}

const homeFeedStore = { daily, playlists, albums, ensureLoaded, refresh };

export function useHomeFeedStore() {
  return homeFeedStore;
}

export function __resetHomeFeedForTest() {
  inFlight = null;
  for (const section of [daily, playlists, albums]) {
    section.items = [];
    section.loading = false;
    section.refreshing = false;
    section.error = null;
    section.loaded = false;
  }
}

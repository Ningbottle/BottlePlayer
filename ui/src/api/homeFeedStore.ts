import { reactive } from 'vue';
import { apiGet } from '../platform/tauri/nativeClient';
import { normalizeTrack, type Track } from '../shared/music/track';

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

export type HomeSection = 'daily' | 'playlists' | 'albums';

interface SectionRequestSession {
  generation: number;
  promise: Promise<void> | null;
}

const HOME_SECTIONS: HomeSection[] = ['daily', 'playlists', 'albums'];

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

const sessions: Record<HomeSection, SectionRequestSession> = {
  daily: { generation: 0, promise: null },
  playlists: { generation: 0, promise: null },
  albums: { generation: 0, promise: null },
};

/** Local calendar day (Y-M-D) of the last successful daily fetch — daily recs
 *  must rotate when the app stays open across midnight. */
let dailyLoadedDay = '';

function todayKey(): string {
  const d = new Date();
  return `${d.getFullYear()}-${d.getMonth() + 1}-${d.getDate()}`;
}

function getSectionState(section: HomeSection): HomeSectionState<Track> | HomeSectionState<PlaylistInfo> {
  if (section === 'daily') return daily;
  return section === 'playlists' ? playlists : albums;
}

function isCurrent(section: HomeSection, generation: number): boolean {
  return sessions[section].generation === generation;
}

function beginLoad(
  section: HomeSection,
  generation: number,
  refreshing: boolean,
): void {
  if (!isCurrent(section, generation)) return;

  const state = getSectionState(section);
  state.error = null;
  state.loading = !refreshing;
  state.refreshing = refreshing;
}

function finishLoad(section: HomeSection, generation: number): void {
  if (!isCurrent(section, generation)) return;

  const state = getSectionState(section);
  state.loading = false;
  state.refreshing = false;
}

function normalizePlaylist(playlist: Record<string, unknown>): PlaylistInfo {
  const imgurl = typeof playlist.imgurl === 'string'
    ? playlist.imgurl.replace('{size}', '400')
    : typeof playlist.pic_url === 'string'
      ? playlist.pic_url.replace('{size}', '400')
      : '';

  return { ...playlist, imgurl } as PlaylistInfo;
}

function responseList(response: unknown, keys: readonly string[]): Record<string, unknown>[] {
  const result = response as { status?: unknown; data?: unknown };
  if (result.status !== 1) {
    throw new Error('home_feed_business_failure');
  }

  const responseData = result.data;
  const data = responseData && typeof responseData === 'object' && 'data' in responseData
    ? (responseData as { data?: unknown }).data
    : responseData;
  if (!data || typeof data !== 'object') {
    throw new Error('home_feed_payload_missing');
  }

  for (const key of keys) {
    const list = (data as Record<string, unknown>)[key];
    if (Array.isArray(list)) return list as Record<string, unknown>[];
  }

  throw new Error('home_feed_list_missing');
}

async function loadDailyItems(): Promise<Track[]> {
  const primaryResponse = await apiGet<unknown>('/everyday/recommend', { pagesize: 6 });
  let primaryItems: Record<string, unknown>[];
  try {
    primaryItems = responseList(primaryResponse, ['song_list', 'info', 'list']);
  } catch {
    // The top-song endpoint is the established fallback for unavailable daily recommendations.
    console.info('[home] daily: /everyday/recommend failed, falling back to static /top/song chart');
    const fallbackResponse = await apiGet<unknown>('/top/song', { pagesize: 6 });
    return responseList(fallbackResponse, ['info', 'list']).slice(0, 6).map(normalizeTrack);
  }

  if (primaryItems.length > 0) {
    return primaryItems.slice(0, 6).map(normalizeTrack);
  }

  console.info('[home] daily: /everyday/recommend returned empty, falling back to static /top/song chart');
  const fallbackResponse = await apiGet<unknown>('/top/song', { pagesize: 6 });
  return responseList(fallbackResponse, ['info', 'list']).slice(0, 6).map(normalizeTrack);
}

async function loadPlaylistItems(sort: number): Promise<PlaylistInfo[]> {
  const response = await apiGet<unknown>('/top/playlist', { pagesize: 5, sort });
  return responseList(response, ['info', 'list']).slice(0, 5).map(normalizePlaylist);
}

async function loadSectionItems(section: HomeSection): Promise<Track[] | PlaylistInfo[]> {
  if (section === 'daily') return loadDailyItems();
  return loadPlaylistItems(section === 'playlists' ? 2 : 5);
}

function commitItems(section: HomeSection, items: Track[] | PlaylistInfo[]): void {
  if (section === 'daily') {
    daily.items = items as Track[];
  } else if (section === 'playlists') {
    playlists.items = items as PlaylistInfo[];
  } else {
    albums.items = items as PlaylistInfo[];
  }
}

function startSection(
  section: HomeSection,
  preferRefreshing: boolean,
  supersede = false,
): Promise<void> {
  const session = sessions[section];
  if (session.promise && !supersede) return session.promise;

  const generation = ++session.generation;
  const state = getSectionState(section);
  const refreshing = preferRefreshing && (state.loaded || state.items.length > 0);
  beginLoad(section, generation, refreshing);

  const promise = (async () => {
    try {
      const items = await loadSectionItems(section);
      if (!isCurrent(section, generation)) return;

      commitItems(section, items);
      const currentState = getSectionState(section);
      currentState.error = null;
      currentState.loaded = true;
      if (section === 'daily') dailyLoadedDay = todayKey();
    } catch {
      if (!isCurrent(section, generation)) return;
      getSectionState(section).error = '加载失败';
    } finally {
      finishLoad(section, generation);
    }
  })();

  session.promise = promise;
  void promise.finally(() => {
    if (isCurrent(section, generation) && session.promise === promise) {
      session.promise = null;
    }
  });
  return promise;
}

/** Dev-only seed for layout screenshots (?layoutDemo=1). Not used in production. */
function isLayoutDemo(): boolean {
  if (!import.meta.env.DEV || typeof window === 'undefined') return false;
  return new URLSearchParams(window.location.search).has('layoutDemo');
}

function seedLayoutDemo(): void {
  for (const session of Object.values(sessions)) {
    session.generation += 1;
    session.promise = null;
  }

  const mkTrack = (i: number) =>
    normalizeTrack({
      FileHash: `demo-track-${i}`,
      SongName: `推荐曲目 ${i}`,
      SingerName: `艺人 ${((i - 1) % 6) + 1}`,
      Duration: 180 + i * 7,
      Image: '',
      AlbumName: `合集 ${i}`,
    });

  const mkPl = (i: number, prefix: string) =>
    normalizePlaylist({
      specialid: 9000 + i,
      specialname: `${prefix} ${i}`,
      imgurl: '',
      nickname: `编辑 ${i}`,
      playcount: 1000 * i,
    });

  daily.items = Array.from({ length: 12 }, (_, i) => mkTrack(i + 1));
  daily.loading = false;
  daily.refreshing = false;
  daily.error = null;
  daily.loaded = true;

  playlists.items = Array.from({ length: 10 }, (_, i) => mkPl(i + 1, '精选歌单'));
  playlists.loading = false;
  playlists.refreshing = false;
  playlists.error = null;
  playlists.loaded = true;

  albums.items = Array.from({ length: 10 }, (_, i) => mkPl(i + 1, '最新歌单'));
  albums.loading = false;
  albums.refreshing = false;
  albums.error = null;
  albums.loaded = true;
}

function ensureLoaded(): Promise<void> {
  if (isLayoutDemo()) {
    seedLayoutDemo();
    return Promise.resolve();
  }

  const unloaded = HOME_SECTIONS.filter((section) => {
    if (!getSectionState(section).loaded) return true;
    return section === 'daily' && dailyLoadedDay !== todayKey();
  });
  return unloaded.length > 0
    ? Promise.all(unloaded.map((section) => startSection(section, false))).then(() => undefined)
    : Promise.resolve();
}

function refresh(): Promise<void> {
  if (isLayoutDemo()) {
    seedLayoutDemo();
    return Promise.resolve();
  }
  return Promise.all(HOME_SECTIONS.map((section) => startSection(section, true, true))).then(() => undefined);
}

function retrySection(section: HomeSection): Promise<void> {
  if (isLayoutDemo()) {
    seedLayoutDemo();
    return Promise.resolve();
  }
  return startSection(section, true, true);
}

const homeFeedStore = { daily, playlists, albums, ensureLoaded, refresh, retrySection };

export function useHomeFeedStore() {
  return homeFeedStore;
}

export function __resetHomeFeedForTest() {
  for (const section of HOME_SECTIONS) {
    const session = sessions[section];
    session.generation += 1;
    session.promise = null;

    const state = getSectionState(section);
    state.items = [];
    state.loading = false;
    state.refreshing = false;
    state.error = null;
    state.loaded = false;
  }
  dailyLoadedDay = '';
}

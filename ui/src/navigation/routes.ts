import type { RouteLocationNormalizedLoaded, RouteRecordRaw } from 'vue-router';

import EqualizerView from '../views/EqualizerView.vue';
import HistoryView from '../views/HistoryView.vue';
import HomeView from '../views/HomeView.vue';
import LoginView from '../views/LoginView.vue';
import LyricView from '../views/LyricView.vue';
import PlaylistView from '../views/PlaylistView.vue';
import SearchView from '../views/SearchView.vue';
import SettingsView from '../views/SettingsView.vue';
import StatsView from '../views/StatsView.vue';
import IslandView from '../views/overlay/IslandView.vue';
import DesktopLyricView from '../views/overlay/DesktopLyricView.vue';

export const routeNames = {
  home: 'home',
  stats: 'stats',
  history: 'history',
  equalizer: 'equalizer',
  settings: 'settings',
  search: 'search',
  playlist: 'playlist',
  lyric: 'lyric',
  login: 'login',
  overlayIsland: 'overlayIsland',
  overlayLyric: 'overlayLyric',
} as const;

export type AppRouteName = typeof routeNames[keyof typeof routeNames];

function queryValue(value: unknown): string {
  if (Array.isArray(value)) return typeof value[0] === 'string' ? value[0] : '';
  return typeof value === 'string' ? value : '';
}

function routeParam(route: RouteLocationNormalizedLoaded, name: string): string {
  return queryValue(route.params[name]);
}

export const routeRecords: RouteRecordRaw[] = [
  { path: '/', name: routeNames.home, component: HomeView, meta: { keepAlive: true } },
  { path: '/stats', name: routeNames.stats, component: StatsView },
  { path: '/history', name: routeNames.history, component: HistoryView },
  { path: '/equalizer', name: routeNames.equalizer, component: EqualizerView },
  { path: '/settings', name: routeNames.settings, component: SettingsView },
  {
    path: '/search',
    name: routeNames.search,
    component: SearchView,
    props: (route) => ({ query: queryValue(route.query.q) }),
  },
  {
    path: '/playlist/:id',
    name: routeNames.playlist,
    component: PlaylistView,
    props: (route) => ({
      playlistId: routeParam(route, 'id'),
      playlistName: queryValue(route.query.name),
      playlistSource: queryValue(route.query.source),
    }),
  },
  { path: '/lyric', name: routeNames.lyric, component: LyricView },
  { path: '/login', name: routeNames.login, component: LoginView },
  { path: '/overlay/island', name: routeNames.overlayIsland, component: IslandView, meta: { overlay: true } },
  { path: '/overlay/lyric', name: routeNames.overlayLyric, component: DesktopLyricView, meta: { overlay: true } },
];

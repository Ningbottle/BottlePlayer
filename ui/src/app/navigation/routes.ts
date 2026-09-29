import type { RouteLocationNormalizedLoaded, RouteRecordRaw } from 'vue-router';

import { HomeView } from '../../features/home';

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
  { path: '/stats', name: routeNames.stats, component: () => import('../../features/stats').then(({ StatsView }) => StatsView) },
  { path: '/history', name: routeNames.history, component: () => import('../../features/library').then(({ HistoryView }) => HistoryView) },
  { path: '/equalizer', name: routeNames.equalizer, component: () => import('../../features/settings').then(({ EqualizerView }) => EqualizerView) },
  { path: '/settings', name: routeNames.settings, component: () => import('../../features/settings').then(({ SettingsView }) => SettingsView) },
  {
    path: '/search',
    name: routeNames.search,
    component: () => import('../../features/search').then(({ SearchView }) => SearchView),
    props: (route) => ({ query: queryValue(route.query.q) }),
  },
  {
    path: '/playlist/:id',
    name: routeNames.playlist,
    component: () => import('../../features/library').then(({ PlaylistView }) => PlaylistView),
    props: (route) => ({
      playlistId: routeParam(route, 'id'),
      playlistName: queryValue(route.query.name),
      playlistSource: queryValue(route.query.source),
    }),
  },
  { path: '/lyric', name: routeNames.lyric, component: () => import('../../features/lyrics').then(({ LyricView }) => LyricView) },
  { path: '/login', name: routeNames.login, component: () => import('../../features/account').then(({ LoginView }) => LoginView) },
  // Direct launches, stale shortcuts, and malformed deep links still need a
  // real page. Keep this unnamed so AppRouteName remains the navigable pages.
  { path: '/:pathMatch(.*)*', redirect: { name: routeNames.home } },
];

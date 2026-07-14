import type { RouteRecordRaw } from 'vue-router';

import EqualizerView from '../views/EqualizerView.vue';
import HistoryView from '../views/HistoryView.vue';
import HomeView from '../views/HomeView.vue';
import LoginView from '../views/LoginView.vue';
import LyricView from '../views/LyricView.vue';
import PlaylistView from '../views/PlaylistView.vue';
import SearchView from '../views/SearchView.vue';
import SettingsView from '../views/SettingsView.vue';
import StatsView from '../views/StatsView.vue';

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

export const routeRecords: RouteRecordRaw[] = [
  { path: '/', name: routeNames.home, component: HomeView, meta: { keepAlive: true } },
  { path: '/stats', name: routeNames.stats, component: StatsView },
  { path: '/history', name: routeNames.history, component: HistoryView },
  { path: '/equalizer', name: routeNames.equalizer, component: EqualizerView },
  { path: '/settings', name: routeNames.settings, component: SettingsView },
  { path: '/search', name: routeNames.search, component: SearchView },
  { path: '/playlist/:id', name: routeNames.playlist, component: PlaylistView },
  { path: '/lyric', name: routeNames.lyric, component: LyricView },
  { path: '/login', name: routeNames.login, component: LoginView },
];

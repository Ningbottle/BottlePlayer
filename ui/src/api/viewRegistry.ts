import type { Component } from 'vue';

import HomeView from '../views/HomeView.vue';
import SearchView from '../views/SearchView.vue';
import PlaylistView from '../views/PlaylistView.vue';
import LyricView from '../views/LyricView.vue';
import SettingsView from '../views/SettingsView.vue';
import LoginView from '../views/LoginView.vue';
import HistoryView from '../views/HistoryView.vue';
import StatsView from '../views/StatsView.vue';
import EqualizerView from '../views/EqualizerView.vue';

export type ViewName =
  | 'home'
  | 'search'
  | 'playlist'
  | 'lyric'
  | 'settings'
  | 'login'
  | 'history'
  | 'stats'
  | 'equalizer';

export interface HistoryEntry {
  view: ViewName;
  playlistId?: string;
  playlistName?: string;
  searchQuery?: string;
  transitionKey?: string;
}

export interface ViewDescriptor {
  name: ViewName;
  component: Component;
  cacheKey: string;
  keepAlive: boolean;
  transitionKey: string;
}

const viewComponents: Record<ViewName, Component> = {
  home: HomeView,
  search: SearchView,
  playlist: PlaylistView,
  lyric: LyricView,
  settings: SettingsView,
  login: LoginView,
  history: HistoryView,
  stats: StatsView,
  equalizer: EqualizerView,
};

let transitionCounter = 0;

function nextTransitionKey(view: ViewName): string {
  transitionCounter += 1;
  return `${view}:${transitionCounter}`;
}

export function resolveViewDescriptor(entry: HistoryEntry): ViewDescriptor {
  const name = entry.view;

  let cacheKey: string;
  let keepAlive: boolean;

  switch (name) {
    case 'home':
      // Stable key so KeepAlive preserves the home instance.
      cacheKey = 'home';
      keepAlive = true;
      break;
    case 'playlist':
      cacheKey = `playlist:${entry.playlistId || ''}`;
      keepAlive = false;
      break;
    case 'search':
      cacheKey = `search:${entry.searchQuery || ''}`;
      keepAlive = false;
      break;
    default:
      // Unique key per navigation so <Transition> always runs enter/leave.
      if (!entry.transitionKey) {
        entry.transitionKey = nextTransitionKey(name);
      }
      cacheKey = entry.transitionKey;
      keepAlive = false;
      break;
  }

  const transitionKey = entry.transitionKey || cacheKey;

  return {
    name,
    component: viewComponents[name],
    cacheKey,
    keepAlive,
    transitionKey,
  };
}

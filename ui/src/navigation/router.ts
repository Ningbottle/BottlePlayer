import {
  createMemoryHistory,
  createRouter,
  type Router,
  type RouterHistory,
} from 'vue-router';

import { routeRecords } from './routes';

export function createAppRouter(history: RouterHistory = createMemoryHistory()): Router {
  return createRouter({
    history,
    routes: routeRecords,
  });
}

export const router = createAppRouter();

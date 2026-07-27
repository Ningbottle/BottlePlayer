import {
  createMemoryHistory,
  createRouter,
  createWebHistory,
  type Router,
  type RouterHistory,
} from 'vue-router';

import { installNavigationLifecycle } from './navigationLifecycle';
import { initNavigationDirection } from './direction';
import { routeRecords } from './routes';

export function createAppRouter(history: RouterHistory = createMemoryHistory()): Router {
  const router = createRouter({
    history,
    routes: routeRecords,
  });
  installNavigationLifecycle(router);
  initNavigationDirection(router);
  return router;
}

export const router = createAppRouter(createWebHistory());

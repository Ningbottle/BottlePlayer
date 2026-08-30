import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(false),
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { initPlayer, initPlayerBackend, playerStore } from '../../playback/playerStore';
import { getMediaRuntime } from '../../api/mediaRuntime';
import appSource from '../../App.vue?raw';
import { routeNames } from '../../navigation/routes';
import { createAppRouter } from '../../navigation/router';

async function waitForHistoryNavigation(router: ReturnType<typeof createAppRouter>, action: () => void) {
  const settled = new Promise<void>((resolve) => {
    const removeAfterEach = router.afterEach(() => {
      removeAfterEach();
      resolve();
    });
  });
  action();
  await settled;
}

describe('initPlayerBackend HTML5 fallback', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    initPlayer();
    playerStore.backend = null;
  });

  it('falls back to HTML5 when both MFS and MFP fail to initialize', async () => {
    await initPlayerBackend();

    expect(playerStore.backend).toBe('html5');
    expect(getMediaRuntime()?.audio).toBeTruthy();
  });

  it('uses the router history for back and forward without an app-owned stack', async () => {
    const manualNavigationIdentifiers = [
      'historyStack',
      'historyIndex',
      'pushHistory',
      'applyHistoryEntry',
      'currentView',
    ].filter((identifier) => new RegExp(`\\b${identifier}\\b`).test(appSource));
    expect.soft(manualNavigationIdentifiers, 'App.vue still owns a second navigation source')
      .toEqual([]);

    const router = createAppRouter();

    await router.push({ name: routeNames.home });
    await router.push({ name: routeNames.search, query: { q: 'ambient' } });
    expect(router.currentRoute.value.fullPath).toBe('/search?q=ambient');

    await waitForHistoryNavigation(router, () => router.back());
    expect(router.currentRoute.value.fullPath).toBe('/');

    await waitForHistoryNavigation(router, () => router.forward());
    expect(router.currentRoute.value.fullPath).toBe('/search?q=ambient');
  });
});

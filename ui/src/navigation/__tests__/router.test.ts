import { mount } from '@vue/test-utils';
import { KeepAlive, nextTick } from 'vue';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { RouterView, type RouteLocationNormalizedLoaded } from 'vue-router';

const pageLifecycle = vi.hoisted(() => ({
  mounts: { home: 0, search: 0, playlist: 0, lyric: 0 },
  unmounts: { home: 0, search: 0, playlist: 0, lyric: 0 },
  nextInstance: { home: 0, search: 0, playlist: 0, lyric: 0 },
}));

vi.mock('../../views/HomeView.vue', async () => {
  const { defineComponent, h, onMounted, onUnmounted } = await import('vue');
  return {
    default: defineComponent({
      name: 'HomeView',
      setup() {
        const instance = ++pageLifecycle.nextInstance.home;
        onMounted(() => pageLifecycle.mounts.home++);
        onUnmounted(() => pageLifecycle.unmounts.home++);
        return () => h('div', { 'data-test': 'page-home', 'data-instance': instance }, 'home');
      },
    }),
  };
});

vi.mock('../../views/SearchView.vue', async () => {
  const { defineComponent, h, onMounted, onUnmounted } = await import('vue');
  return {
    default: defineComponent({
      name: 'SearchView',
      setup() {
        const instance = ++pageLifecycle.nextInstance.search;
        onMounted(() => pageLifecycle.mounts.search++);
        onUnmounted(() => pageLifecycle.unmounts.search++);
        return () => h('div', { 'data-test': 'page-search', 'data-instance': instance }, 'search');
      },
    }),
  };
});

vi.mock('../../views/PlaylistView.vue', async () => {
  const { defineComponent, h, onMounted, onUnmounted } = await import('vue');
  return {
    default: defineComponent({
      name: 'PlaylistView',
      setup() {
        const instance = ++pageLifecycle.nextInstance.playlist;
        onMounted(() => pageLifecycle.mounts.playlist++);
        onUnmounted(() => pageLifecycle.unmounts.playlist++);
        return () => h('div', { 'data-test': 'page-playlist', 'data-instance': instance }, 'playlist');
      },
    }),
  };
});

vi.mock('../../views/LyricView.vue', async () => {
  const { defineComponent, h, onMounted, onUnmounted } = await import('vue');
  return {
    default: defineComponent({
      name: 'LyricView',
      setup() {
        const instance = ++pageLifecycle.nextInstance.lyric;
        onMounted(() => pageLifecycle.mounts.lyric++);
        onUnmounted(() => pageLifecycle.unmounts.lyric++);
        return () => h('div', { 'data-test': 'page-lyric', 'data-instance': instance }, 'lyric');
      },
    }),
  };
});

vi.mock('../../components/Sidebar.vue', () => ({
  default: { name: 'Sidebar', template: '<nav />' },
}));
vi.mock('../../components/Topbar.vue', () => ({
  default: { name: 'Topbar', template: '<header />' },
}));
vi.mock('../../components/PlayerBar.vue', () => ({
  default: { name: 'PlayerBar', template: '<footer />' },
}));
vi.mock('../../components/Drawer.vue', () => ({
  default: { name: 'Drawer', template: '<aside />' },
}));
vi.mock('../../components/QueuePanel.vue', () => ({
  default: { name: 'QueuePanel', template: '<aside />' },
}));
vi.mock('../../components/shell/FullscreenWindowControls.vue', () => ({
  default: { name: 'FullscreenWindowControls', template: '<div />' },
}));

vi.mock('../../components/shell/AuroraShell.vue', () => ({
  default: {
    name: 'AuroraShell',
    template: '<div><slot name="sidebar"/><slot name="topbar"/><slot/><slot name="extras"/><slot name="playerbar"/></div>',
  },
}));
vi.mock('../../components/shell/NewsprintShell.vue', () => ({
  default: {
    name: 'NewsprintShell',
    template: '<div><slot name="sidebar"/><slot name="topbar"/><slot/><slot name="extras"/><slot name="playerbar"/></div>',
  },
}));
vi.mock('../../api/playerStore', () => ({ initPlayer: vi.fn(), initPlayerBackend: vi.fn() }));
vi.mock('../../api/userStore', () => ({ checkLoginStatus: vi.fn() }));
vi.mock('../../api/backend', () => ({ ping: vi.fn().mockResolvedValue(true) }));
vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(0) }));
vi.mock('../../api/motion', () => ({ transitionEnter: vi.fn(), transitionLeave: vi.fn() }));
vi.mock('../../api/themeStore', async () => {
  const { ref } = await import('vue');
  const skinId = ref('aurora');
  return { useThemeStore: () => ({ skinId }) };
});
vi.mock('../../api/lyricFullscreen', async () => {
  const { ref } = await import('vue');
  return { lyricFullscreen: ref(false), setLyricFullscreen: vi.fn() };
});

import HomeView from '../../views/HomeView.vue';
import LyricView from '../../views/LyricView.vue';
import PlaylistView from '../../views/PlaylistView.vue';
import SearchView from '../../views/SearchView.vue';
import App from '../../App.vue';
import { routeNames, routeRecords } from '../routes';
import { createAppRouter } from '../router';

beforeEach(() => {
  for (const key of ['home', 'search', 'playlist', 'lyric'] as const) {
    pageLifecycle.mounts[key] = 0;
    pageLifecycle.unmounts[key] = 0;
    pageLifecycle.nextInstance[key] = 0;
  }
});

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

function getRoute(name: string) {
  const route = routeRecords.find((record) => record.name === name);
  expect(route, `route ${name} should be registered`).toBeDefined();
  return route!;
}

function resolveProps(
  routeName: string,
  location: Pick<RouteLocationNormalizedLoaded, 'params' | 'query'>,
) {
  const props = getRoute(routeName).props;
  expect.soft(typeof props, `${routeName} should map route state to component props`).toBe('function');
  if (typeof props !== 'function') return {};
  return (props as (route: RouteLocationNormalizedLoaded) => Record<string, unknown>)({
    ...location,
  } as RouteLocationNormalizedLoaded);
}

describe('navigation route contract', () => {
  it('registers page names and maps the core page components', () => {
    expect(getRoute(routeNames.home).component).toBe(HomeView);
    expect(getRoute(routeNames.search).component).toBe(SearchView);
    expect(getRoute(routeNames.playlist).component).toBe(PlaylistView);
    expect(getRoute(routeNames.lyric).component).toBe(LyricView);

    expect(getRoute(routeNames.home).path).toBe('/');
    expect(getRoute(routeNames.search).path).toBe('/search');
    expect(getRoute(routeNames.playlist).path).toBe('/playlist/:id');
    expect(getRoute(routeNames.lyric).path).toBe('/lyric');
  });

  it('maps search query and playlist params/query to their page props', () => {
    expect.soft(resolveProps(routeNames.search, {
      params: {},
      query: { q: 'jazz' },
    })).toMatchObject({ query: 'jazz' });

    expect.soft(resolveProps(routeNames.playlist, {
      params: { id: 'playlist-42' },
      query: { name: 'Evening Mix' },
    })).toMatchObject({
      playlistId: 'playlist-42',
      playlistName: 'Evening Mix',
    });
  });

  it('uses one native history for back and forward without a second stack', async () => {
    const router = createAppRouter();
    const visited: string[] = [];
    const removeAfterEach = router.afterEach((to) => {
      if (typeof to.name === 'string') visited.push(to.name);
    });

    await router.push({ name: routeNames.home });
    await router.push({ name: routeNames.search, query: { q: 'jazz' } });
    await router.push({
      name: routeNames.playlist,
      params: { id: 'playlist-42' },
      query: { name: 'Evening Mix' },
    });

    await waitForHistoryNavigation(router, () => router.back());
    expect(router.currentRoute.value.name).toBe(routeNames.search);
    await waitForHistoryNavigation(router, () => router.forward());
    expect(router.currentRoute.value.name).toBe(routeNames.playlist);
    expect(visited).toEqual([
      routeNames.home,
      routeNames.search,
      routeNames.playlist,
      routeNames.search,
      routeNames.playlist,
    ]);
    expect(router).not.toHaveProperty('historyStack');

    removeAfterEach();
  });

  it('settles repeated navigation without leaving a pending transition', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });

    const result = await Promise.race([
      router.push({ name: routeNames.home }).then(() => 'settled'),
      new Promise<'timed-out'>((resolve) => setTimeout(() => resolve('timed-out'), 100)),
    ]);

    expect(result).toBe('settled');
    expect(router.currentRoute.value.name).toBe(routeNames.home);
  });

  it('marks only home as keepAlive', () => {
    expect(getRoute(routeNames.home).meta?.keepAlive).toBe(true);
    for (const name of [routeNames.search, routeNames.playlist, routeNames.lyric]) {
      expect(getRoute(name).meta?.keepAlive ?? false).toBe(false);
    }
  });

  it('reuses the home instance and unmounts transient route components', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();

    const wrapper = mount(App, { global: { plugins: [router] } });
    await nextTick();

    try {
      expect.soft(wrapper.findComponent(RouterView).exists(), 'App should render RouterView').toBe(true);
      expect.soft(wrapper.findComponent(KeepAlive).exists(), 'App should render a KeepAlive branch').toBe(true);

      const initialHome = wrapper.find('[data-test="page-home"]');
      expect.soft(initialHome.exists()).toBe(true);
      const initialHomeInstance = initialHome.attributes('data-instance');
      expect.soft(pageLifecycle.mounts.home).toBe(1);
      expect.soft(pageLifecycle.unmounts.home).toBe(0);

      await router.push({ name: routeNames.search, query: { q: 'jazz' } });
      await nextTick();
      expect.soft(pageLifecycle.mounts.search, 'search should mount when entered').toBe(1);
      expect.soft(pageLifecycle.unmounts.search).toBe(0);
      expect.soft(pageLifecycle.unmounts.home, 'home should be cached, not unmounted').toBe(0);

      await router.push({ name: routeNames.home });
      await nextTick();
      expect.soft(pageLifecycle.unmounts.search, 'search should unmount when left').toBe(1);
      expect.soft(pageLifecycle.mounts.home, 'home should reuse its first instance').toBe(1);
      expect.soft(wrapper.find('[data-test="page-home"]').attributes('data-instance'))
        .toBe(initialHomeInstance);

      await router.push({
        name: routeNames.playlist,
        params: { id: 'playlist-42' },
        query: { name: 'Evening Mix' },
      });
      await nextTick();
      expect.soft(pageLifecycle.mounts.playlist, 'playlist should mount when entered').toBe(1);

      await router.push({ name: routeNames.lyric });
      await nextTick();
      expect.soft(pageLifecycle.unmounts.playlist, 'playlist should unmount when left').toBe(1);
      expect.soft(pageLifecycle.mounts.lyric, 'lyric should mount when entered').toBe(1);

      await router.push({ name: routeNames.home });
      await nextTick();
      expect.soft(pageLifecycle.unmounts.lyric, 'lyric should unmount when left').toBe(1);
      expect.soft(pageLifecycle.mounts.home, 'home should still have one mounted instance').toBe(1);

      const returnedHome = wrapper.find('[data-test="page-home"]');
      expect.soft(returnedHome.exists()).toBe(true);
      expect.soft(returnedHome.attributes('data-instance')).toBe(initialHomeInstance);
    } finally {
      wrapper.unmount();
    }
  });
});

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
      emits: ['navigate'],
      setup(_props, { emit }) {
        const instance = ++pageLifecycle.nextInstance.home;
        onMounted(() => pageLifecycle.mounts.home++);
        onUnmounted(() => pageLifecycle.unmounts.home++);
        return () => h('div', { 'data-test': 'page-home', 'data-instance': instance }, [
          h('button', { 'data-test': 'home-playlist', onClick: () => emit('navigate', 'playlist', { id: 'playlist-42', name: 'Evening Mix' }) }, 'playlist'),
          h('button', { 'data-test': 'home-lyric', onClick: () => emit('navigate', 'lyric') }, 'lyric'),
        ]);
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
      props: ['isQueueOpen'],
      emits: ['navigate'],
      setup(props, { emit }) {
        const instance = ++pageLifecycle.nextInstance.lyric;
        onMounted(() => pageLifecycle.mounts.lyric++);
        onUnmounted(() => pageLifecycle.unmounts.lyric++);
        return () => h('div', {
          'data-test': 'page-lyric',
          'data-instance': instance,
          'data-queue-open': String(props.isQueueOpen),
        }, [
          'lyric',
          h('button', { 'data-test': 'lyric-home', onClick: () => emit('navigate', 'home') }, 'home'),
          h('button', { 'data-test': 'lyric-search', onClick: () => emit('navigate', 'search') }, 'search'),
        ]);
      },
    }),
  };
});

vi.mock('../../views/LoginView.vue', async () => {
  const { defineComponent, h } = await import('vue');
  return {
    default: defineComponent({
      name: 'LoginView',
      emits: ['navigate'],
      setup(_props, { emit }) {
        return () => h('button', { 'data-test': 'login-home', onClick: () => emit('navigate', 'home') }, 'home');
      },
    }),
  };
});

vi.mock('../../views/SettingsView.vue', () => ({
  default: { name: 'SettingsView', template: '<div data-test="page-settings" />' },
}));

vi.mock('../../components/Sidebar.vue', () => ({
  default: { name: 'Sidebar', emits: ['navigate'], template: '<nav><button data-test="sidebar-search" @click="$emit(\'navigate\', \'search\')" /></nav>' },
}));
vi.mock('../../components/Topbar.vue', () => ({
  default: {
    name: 'Topbar',
    emits: ['search', 'navigate', 'back', 'forward'],
    template: '<header><button data-test="topbar-search" @click="$emit(\'search\', \'jazz\')" /><button data-test="topbar-settings" @click="$emit(\'navigate\', \'settings\')" /><button data-test="topbar-back" @click="$emit(\'back\')" /><button data-test="topbar-forward" @click="$emit(\'forward\')" /></header>',
  },
}));
vi.mock('../../components/PlayerBar.vue', () => ({
  default: {
    name: 'PlayerBar',
    props: ['navigate'],
    emits: ['navigate', 'toggle-queue'],
    template: '<footer><button data-test="player-lyric" @click="navigate ? navigate(\'lyric\') : $emit(\'navigate\', \'lyric\')" /><button data-test="player-queue" @click="$emit(\'toggle-queue\')" /></footer>',
  },
}));
vi.mock('../../components/QueuePanel.vue', () => ({
  default: { name: 'QueuePanel', template: '<aside data-test="queue-panel" />' },
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
vi.mock('../../playback/playerStore', () => ({ initPlayer: vi.fn(), initPlayerBackend: vi.fn() }));
vi.mock('../../api/userStore', () => ({ checkLoginStatus: vi.fn() }));
vi.mock('../../platform/tauri/nativeClient', () => ({ ping: vi.fn().mockResolvedValue(true) }));
vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(0) }));
vi.mock('../../api/motion', () => ({ transitionEnter: vi.fn(), transitionLeave: vi.fn(), isReducedMotion: vi.fn(() => true) }));
vi.mock('../../api/themeStore', async () => {
  const { ref } = await import('vue');
  const skinId = ref('aurora');
  return { useThemeStore: () => ({ skinId }) };
});
vi.mock('../../api/lyricFullscreen', async () => {
  const { ref } = await import('vue');
  const lyricFullscreen = ref(false);
  return {
    lyricFullscreen,
    setLyricFullscreen: vi.fn((value: boolean) => {
      lyricFullscreen.value = value;
    }),
    clearLyricFullscreenUnlessOnLyric: vi.fn((isLyricRoute: boolean) => {
      if (!isLyricRoute) lyricFullscreen.value = false;
    }),
  };
});

import HomeView from '../../views/HomeView.vue';
import LyricView from '../../views/LyricView.vue';
import LoginView from '../../views/LoginView.vue';
import PlaylistView from '../../views/PlaylistView.vue';
import SearchView from '../../views/SearchView.vue';
import App from '../../App.vue';
import { initPlayer, initPlayerBackend } from '../../playback/playerStore';
import { registerPageTransition } from '../navigationLifecycle';
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

async function clickAndWaitForNavigation(
  router: ReturnType<typeof createAppRouter>,
  wrapper: ReturnType<typeof mount>,
  selector: string,
) {
  const settled = new Promise<void>((resolve) => {
    const removeAfterEach = router.afterEach(() => {
      removeAfterEach();
      resolve();
    });
  });
  await wrapper.get(selector).trigger('click');
  await settled;
  await nextTick();
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
    expect(routeRecords.map((record) => record.name)).toEqual(Object.values(routeNames));
    expect(routeRecords.map((record) => record.path)).toEqual([
      '/',
      '/stats',
      '/history',
      '/equalizer',
      '/settings',
      '/search',
      '/playlist/:id',
      '/lyric',
      '/login',
      '/overlay/island',
      '/overlay/lyric',
    ]);
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

  it('cleans only registered page transition nodes during navigation', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    const owned = document.createElement('div');
    const unrelated = document.createElement('div');
    owned.style.opacity = '0';
    owned.style.transform = 'translateY(16px)';
    owned.style.filter = 'blur(2px)';
    unrelated.style.opacity = '0.5';
    registerPageTransition(owned);

    await router.push({ name: routeNames.search, query: { q: 'jazz' } });

    expect(owned.style.opacity).toBe('');
    expect(owned.style.transform).toBe('');
    expect(owned.style.filter).toBe('');
    expect(unrelated.style.opacity).toBe('0.5');
  });

  it('marks only home as keepAlive', () => {
    expect(getRoute(routeNames.home).meta?.keepAlive).toBe(true);
    for (const name of [
      routeNames.stats,
      routeNames.history,
      routeNames.equalizer,
      routeNames.settings,
      routeNames.search,
      routeNames.playlist,
      routeNames.lyric,
      routeNames.login,
    ]) {
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

  it('routes page emits through the App adapter and provides lyric queue state', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();
    const wrapper = mount(App, { global: { plugins: [router], stubs: { Transition: false } } });

    try {
      await clickAndWaitForNavigation(router, wrapper, '[data-test="home-playlist"]');
      expect(router.currentRoute.value.name).toBe(routeNames.playlist);

      await router.push({ name: routeNames.home });
      await nextTick();
      await clickAndWaitForNavigation(router, wrapper, '[data-test="home-lyric"]');
      expect(router.currentRoute.value.name).toBe(routeNames.lyric);
      await wrapper.get('[data-test="player-queue"]').trigger('click');
      await nextTick();
      expect(wrapper.get('[data-test="page-lyric"]').attributes('data-queue-open')).toBe('true');

      await clickAndWaitForNavigation(router, wrapper, '[data-test="lyric-home"]');
      expect(router.currentRoute.value.name).toBe(routeNames.home);
      await router.push({ name: routeNames.lyric });
      await nextTick();
      await clickAndWaitForNavigation(router, wrapper, '[data-test="lyric-search"]');
      expect(router.currentRoute.value.name).toBe(routeNames.search);

      await router.push({ name: routeNames.login });
      await nextTick();
      expect(wrapper.findComponent(LoginView).exists()).toBe(true);
      await clickAndWaitForNavigation(router, wrapper, '[data-test="login-home"]');
      expect(router.currentRoute.value.name).toBe(routeNames.home);
    } finally {
      wrapper.unmount();
    }
  });

  it('uses App adapters as the sole navigation command owner', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();
    const visited: string[] = [];
    const removeAfterEach = router.afterEach((to) => visited.push(String(to.name)));
    const wrapper = mount(App, { global: { plugins: [router] } });

    try {
      await clickAndWaitForNavigation(router, wrapper, '[data-test="sidebar-search"]');
      expect(router.currentRoute.value.name).toBe(routeNames.search);
      expect(visited).toEqual([routeNames.search]);

      visited.length = 0;
      await clickAndWaitForNavigation(router, wrapper, '[data-test="topbar-settings"]');
      expect(router.currentRoute.value.name).toBe(routeNames.settings);
      expect(visited).toEqual([routeNames.settings]);

      visited.length = 0;
      await clickAndWaitForNavigation(router, wrapper, '[data-test="player-lyric"]');
      expect(router.currentRoute.value.name).toBe(routeNames.lyric);
      expect(visited).toEqual([routeNames.lyric]);

      await clickAndWaitForNavigation(router, wrapper, '[data-test="topbar-back"]');
      expect(router.currentRoute.value.name).toBe(routeNames.settings);
      await clickAndWaitForNavigation(router, wrapper, '[data-test="topbar-forward"]');
      expect(router.currentRoute.value.name).toBe(routeNames.lyric);
    } finally {
      removeAfterEach();
      wrapper.unmount();
    }
  });

  it('runs page transition hooks from the RouterView slot', async () => {
    const { transitionEnter, transitionLeave } = await import('../../api/motion');
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();
    const wrapper = mount(App, { global: { plugins: [router], stubs: { Transition: false } } });

    try {
      await router.push({ name: routeNames.search, query: { q: 'jazz' } });
      await nextTick();
      await nextTick();
      expect(transitionLeave).toHaveBeenCalled();
      expect(transitionEnter).toHaveBeenCalled();
    } finally {
      wrapper.unmount();
    }
  });

  it('does not boot a player when the window URL is an overlay path even if the router is still on a page route', async () => {
    vi.mocked(initPlayer).mockClear();
    vi.mocked(initPlayerBackend).mockClear();
    const previous = `${location.pathname}${location.search}${location.hash}`;
    window.history.replaceState({}, '', '/overlay/island');
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();
    const wrapper = mount(App, { global: { plugins: [router] } });
    try {
      await nextTick();
      expect(initPlayer, 'overlay windows must not call initPlayer').not.toHaveBeenCalled();
      expect(initPlayerBackend, 'overlay windows must not call initPlayerBackend').not.toHaveBeenCalled();
    } finally {
      wrapper.unmount();
      window.history.replaceState({}, '', previous || '/');
    }
  });

  it('App shell does not render Drawer and still renders QueuePanel', async () => {
    const router = createAppRouter();
    await router.push({ name: routeNames.home });
    await router.isReady();
    const wrapper = mount(App, { global: { plugins: [router] } });

    try {
      await nextTick();
      expect(wrapper.find('.drawer').exists(), 'Drawer component should not be rendered').toBe(false);
      expect(wrapper.find('[data-test="queue-panel"]').exists(), 'QueuePanel should be rendered in the extras slot').toBe(true);
    } finally {
      wrapper.unmount();
    }
  });
});

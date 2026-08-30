import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { enableAutoUnmount, flushPromises, mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Sidebar from '../Sidebar.vue';
import { apiGet } from '../../platform/tauri/nativeClient';
import { userStore } from '../../api/userStore';
import { useThemeStore, __resetForTest } from '../../app/appearance/themeStore';
import { createAppRouter } from '../../app/navigation/router';
import { routeNames } from '../../app/navigation/routes';

vi.mock('@tauri-apps/plugin-updater', () => ({
  check: vi.fn().mockResolvedValue(null),
}));

vi.mock('../../platform/tauri/nativeClient', () => ({
  apiGet: vi.fn().mockResolvedValue({}),
}));

vi.mock('../../api/userStore', async () => {
  const { reactive } = await vi.importActual<typeof import('vue')>('vue');
  return {
    userStore: reactive({
      isLoggedIn: false,
      deviceReady: false,
      userId: '',
      username: '未登录',
      avatar: '',
      isVip: false,
      vipLevel: 0,
    }),
  };
});

vi.mock('../../api/favoriteStore', async () => {
  const actual = await vi.importActual<typeof import('../../api/favoriteStore')>('../../api/favoriteStore');
  return {
    normalizePlaylists: actual.normalizePlaylists,
  };
});

vi.mock('../../app/update/skippedVersion', async () => {
  const { ref } = await vi.importActual<typeof import('vue')>('vue');
  return {
    useSkippedVersion: () => ref(''),
    getSkippedVersion: () => '',
  };
});

async function mountSidebar(initialRoute = routeNames.home) {
  const router = createAppRouter();
  await router.push({ name: initialRoute });
  await router.isReady();
  return {
    router,
    wrapper: mount(Sidebar, { global: { plugins: [router] } }),
  };
}

describe('Sidebar', () => {
  beforeEach(() => {
    vi.mocked(apiGet).mockReset();
    vi.mocked(apiGet).mockResolvedValue({});
    userStore.isLoggedIn = false;
    userStore.deviceReady = false;
    userStore.userId = '';
    userStore.username = '未登录';
    userStore.avatar = '';
    userStore.isVip = false;
    userStore.vipLevel = 0;
  });

  it('places the equalizer navigation entry directly below recent-play and navigates to it', async () => {
    const { router, wrapper } = await mountSidebar();

    const navEntries = wrapper.findAll('.nav > a');
    expect(navEntries).toHaveLength(4);
    expect(navEntries[2].text()).toContain('最近播放');
    expect(navEntries[3].text()).toContain('均衡器');

    const navChildren = Array.from(wrapper.get('.nav').element.children);
    const recentIndex = navChildren.findIndex((child) => child.textContent?.includes('最近播放'));
    const eqIndex = navChildren.findIndex((child) => child.textContent?.includes('均衡器'));

    expect(eqIndex).toBe(recentIndex + 1);

    await navEntries[3].trigger('click');
    expect(wrapper.emitted('navigate')?.[0]).toEqual(['equalizer']);
    expect(router.currentRoute.value.name).toBe(routeNames.home);
  });

  it('does not request playlists before the authenticated device is ready', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = 'user-1';
    userStore.deviceReady = false;
    const { wrapper } = await mountSidebar();
    await flushPromises();

    expect(apiGet).not.toHaveBeenCalledWith('/user/playlist', expect.anything());

    userStore.deviceReady = true;
    await flushPromises();

    expect(apiGet).toHaveBeenCalledWith('/user/playlist', { page: 1, pagesize: 100 });
    wrapper.unmount();
  });

  it('keeps the last good playlists when a refresh returns a business error', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = 'user-1';
    userStore.deviceReady = true;
    vi.mocked(apiGet)
      .mockResolvedValueOnce({
        status: 1,
        data: { info: [{ global_collection_id: 'collection_3_1_1_0', listid: '1', name: '我喜欢' }] },
      })
      .mockResolvedValueOnce({
        status: 0,
        error_code: 20017,
        data: { info: [] },
      });

    const { wrapper } = await mountSidebar();
    await flushPromises();
    expect(wrapper.text()).toContain('我喜欢');

    userStore.deviceReady = false;
    await nextTick();
    userStore.deviceReady = true;
    await flushPromises();

    expect(wrapper.text()).toContain('我喜欢');
    expect(wrapper.get('[data-test="playlist-retry"]').text()).toContain('歌单加载失败');
    expect(wrapper.get('[data-test="playlist-retry"]').attributes('title')).toContain('20017');
    wrapper.unmount();
  });

  it('navigates user playlists with global_collection_id, not numeric listid', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = 'user-1';
    userStore.deviceReady = true;
    vi.mocked(apiGet).mockResolvedValue({
      status: 1,
      data: {
        info: [{
          global_collection_id: 'collection_3_42_98765_0',
          listid: '98765',
          listname: '收藏歌单',
        }],
      },
    });

    const { wrapper } = await mountSidebar();
    await flushPromises();
    await wrapper.get('[data-test="sidebar-user-playlist"]').trigger('click');
    expect(wrapper.emitted('navigate')?.[0]).toEqual([
      'playlist',
      { id: 'collection_3_42_98765_0', name: '收藏歌单', source: 'user' },
    ]);
    wrapper.unmount();
  });

  it('shows a contract error instead of 暂无歌单 when every item lacks a GID', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = 'user-1';
    userStore.deviceReady = true;
    vi.mocked(apiGet).mockResolvedValue({
      status: 0,
      error_code: 'native_user_playlist_id_contract_invalid',
      data: { list: [], skipped_invalid_id_count: 2 },
    });

    const { wrapper } = await mountSidebar();
    await flushPromises();
    expect(wrapper.text()).toContain('歌单加载失败');
    expect(wrapper.get('[data-test="playlist-retry"]').attributes('title')).toContain('缺少 global_collection_id');
    expect(wrapper.text()).not.toContain('暂无歌单');
    wrapper.unmount();
  });

  it('shows 暂无歌单 for a successful empty list', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = 'user-1';
    userStore.deviceReady = true;
    vi.mocked(apiGet).mockResolvedValue({
      status: 1,
      data: { info: [], list: [], total: 0, skipped_invalid_id_count: 0 },
    });

    const { wrapper } = await mountSidebar();
    await flushPromises();
    expect(wrapper.text()).toContain('暂无歌单');
    expect(wrapper.find('[data-test="playlist-retry"]').exists()).toBe(false);
    wrapper.unmount();
  });
});

enableAutoUnmount(afterEach);

describe('Sidebar skin chrome', () => {
  beforeEach(() => {
    __resetForTest();
    localStorage.clear();
    useThemeStore().init();
  });

  it('marks chrome for aurora and uses pill active nav without newsprint stamp footer', async () => {
    useThemeStore().setSkin('aurora');
    const { router, wrapper } = await mountSidebar();
    await nextTick();
    const root = wrapper.get('[data-test="sidebar-chrome"]');
    expect(root.attributes('data-skin-chrome')).toBe('aurora');
    expect(wrapper.find('[data-test="sidebar-nav-active-pill"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="newsprint-stamp"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="aurora-nav-label"]').exists()).toBe(true);
    expect(wrapper.get('[data-test="sidebar-brand"]').text()).toContain('BottleMusic');
    expect(wrapper.get('[data-test="sidebar-skin-label"]').text()).toBe('极光 Aurora');
    const masthead = root.get('.masthead');
    const wordmark = masthead.get('.sidebar-wordmark');
    expect(wordmark.find('[data-test="sidebar-brand"]').exists()).toBe(true);
    expect(wordmark.find('[data-test="sidebar-skin-label"]').exists()).toBe(true);
    expect(root.get('[data-test="aurora-nav-label"]').element.parentElement).toBe(masthead.element);
    await router.push({ name: routeNames.stats });
    await nextTick();
    expect(wrapper.findAll('[data-test="sidebar-nav-item"]')[1].classes()).toContain('active');
  });

  it('keeps the original Newsprint icon navigation and stamp footer, without Aurora chrome', async () => {
    useThemeStore().setSkin('newsprint');
    const { wrapper } = await mountSidebar();
    await nextTick();
    const root = wrapper.get('[data-test="sidebar-chrome"]');
    expect(root.attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.find('[data-test="sidebar-nav-index"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="newsprint-stamp"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="sidebar-nav-active-pill"]').exists()).toBe(false);
    expect(wrapper.get('[data-test="sidebar-brand"]').text()).toContain('BottleMusic');
    expect(wrapper.get('[data-test="sidebar-skin-label"]').text()).toBe('报刊 Newsprint');
    expect(wrapper.get('[data-test="newsprint-stamp"] .stamp').text()).toBe('印');
    expect(wrapper.get('[data-test="newsprint-stamp"]').findAll('div')[1].text()).toBe('每日刊印始于 2026');
    expect(wrapper.text()).not.toContain('The Player');
  });

  it('still emits navigate without duplicating API surface', async () => {
    useThemeStore().setSkin('aurora');
    const { router, wrapper } = await mountSidebar();
    await wrapper.findAll('.nav a, [data-test="sidebar-nav-item"]')[0].trigger('click');
    expect(wrapper.emitted('navigate')?.[0]).toEqual(['home']);
    expect(router.currentRoute.value.name).toBe(routeNames.home);
  });
});

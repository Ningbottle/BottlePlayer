import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Sidebar from '../Sidebar.vue';
import { useThemeStore, __resetForTest } from '../../api/themeStore';
import { createAppRouter } from '../../navigation/router';
import { routeNames } from '../../navigation/routes';

vi.mock('@tauri-apps/plugin-updater', () => ({
  check: vi.fn().mockResolvedValue(null),
}));

vi.mock('../../api/backend', () => ({
  apiGet: vi.fn().mockResolvedValue({}),
}));

vi.mock('../../api/userStore', () => ({
  userStore: {
    isLoggedIn: false,
    userId: '',
    username: '未登录',
    avatar: '',
    isVip: false,
    vipLevel: 0,
  },
}));

vi.mock('../../api/favorite', () => ({
  normalizePlaylists: vi.fn(() => []),
}));

vi.mock('../../api/skippedVersion', async () => {
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
});

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

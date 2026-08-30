import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Topbar from '../../app/shell/Topbar.vue';
import { useThemeStore, __resetForTest } from '../../app/appearance/themeStore';
import { createAppRouter } from '../../app/navigation/router';
import { routeNames } from '../../app/navigation/routes';

vi.mock('../../api/userStore', () => ({
  userStore: {
    isLoggedIn: false,
    username: '未登录',
    isVip: false,
    avatar: '',
  },
}));

async function mountTopbar(query?: string) {
  const router = createAppRouter();
  await router.push(query
    ? { name: routeNames.search, query: { q: query } }
    : { name: routeNames.home });
  await router.isReady();
  return {
    router,
    wrapper: mount(Topbar, { global: { plugins: [router] } }),
  };
}

describe('Topbar skin chrome', () => {
  beforeEach(() => {
    __resetForTest();
    localStorage.clear();
    useThemeStore().init();
  });

  it('marks aurora topbar command field', async () => {
    useThemeStore().setSkin('aurora');
    const { wrapper } = await mountTopbar();
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('aurora');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('command');
  });

  it('keeps the original rounded Newsprint search field with Chinese copy', async () => {
    useThemeStore().setSkin('newsprint');
    const { wrapper } = await mountTopbar();
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('legacy');
    expect(wrapper.get('input').attributes('placeholder')).toBe('搜索歌曲、艺人、专辑、歌单');
  });

  it('emits search on enter for either skin', async () => {
    useThemeStore().setSkin('aurora');
    const { wrapper } = await mountTopbar('test');
    await wrapper.get('input').trigger('keyup.enter');
    expect(wrapper.emitted('search')?.[0]).toEqual(['test']);
  });

  it('emits back and forward commands without moving router history directly', async () => {
    const { router, wrapper } = await mountTopbar('test');
    await wrapper.findAll('.nav-arrows button')[0].trigger('click');
    await wrapper.findAll('.nav-arrows button')[1].trigger('click');
    expect(wrapper.emitted('back')?.[0]).toEqual([]);
    expect(wrapper.emitted('forward')?.[0]).toEqual([]);
    expect(router.currentRoute.value.name).toBe(routeNames.search);
  });

  it('does not have a toggle-tweaks button', async () => {
    const { wrapper } = await mountTopbar();
    const tweakBtn = wrapper.findAll('button').find((b) => b.attributes('aria-label') === '调整');
    expect(tweakBtn).toBeUndefined();
  });

  it('does not expose a fake desktop share action', async () => {
    const { wrapper } = await mountTopbar();
    const shareButton = wrapper.findAll('button').find((button) => button.attributes('aria-label') === '分享');
    expect(shareButton).toBeUndefined();
  });
});

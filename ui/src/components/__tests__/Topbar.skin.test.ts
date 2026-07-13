import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Topbar from '../Topbar.vue';
import { useThemeStore, __resetForTest } from '../../api/themeStore';

vi.mock('../../api/userStore', () => ({
  userStore: {
    isLoggedIn: false,
    username: '未登录',
    isVip: false,
    avatar: '',
  },
}));

describe('Topbar skin chrome', () => {
  beforeEach(() => {
    __resetForTest();
    localStorage.clear();
    useThemeStore().init();
  });

  it('marks aurora topbar command field', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Topbar, {
      props: { searchQuery: '' },
    });
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('aurora');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('command');
  });

  it('keeps the original rounded Newsprint search field with Chinese copy', async () => {
    useThemeStore().setSkin('newsprint');
    const wrapper = mount(Topbar, {
      props: { searchQuery: '' },
    });
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('legacy');
    expect(wrapper.get('input').attributes('placeholder')).toBe('搜索歌曲、艺人、专辑、歌单');
  });

  it('emits search on enter for either skin', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Topbar, { props: { searchQuery: 'test' } });
    await wrapper.get('input').trigger('keyup.enter');
    expect(wrapper.emitted('search')?.[0]).toEqual(['test']);
  });
});

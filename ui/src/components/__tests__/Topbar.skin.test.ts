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

  it('marks newsprint topbar editorial field', async () => {
    useThemeStore().setSkin('newsprint');
    const wrapper = mount(Topbar, {
      props: { searchQuery: '' },
    });
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('editorial');
  });

  it('emits search on enter for either skin', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Topbar, { props: { searchQuery: 'test' } });
    await wrapper.get('input').trigger('keyup.enter');
    expect(wrapper.emitted('search')?.[0]).toEqual(['test']);
  });
});

import { describe, it, expect, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import Sidebar from '../Sidebar.vue';

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

describe('Sidebar', () => {
  it('places the equalizer navigation entry directly below recent-play and navigates to it', async () => {
    const wrapper = mount(Sidebar, { props: { activeView: 'home' } });

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
  });
});

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';

const backendHealthMock = vi.hoisted(() => vi.fn());

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(0),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    close: vi.fn(),
    minimize: vi.fn(),
    toggleMaximize: vi.fn(),
  }),
}));

vi.mock('../../api/backend', () => ({
  backendHealth: backendHealthMock,
  isCircuitOpen: () => true,
}));

vi.mock('../../api/playerStore', () => ({
  initPlayer: vi.fn(),
  initPlayerBackend: vi.fn(),
  playerStore: {
    currentTrack: null,
    isPlaying: false,
    currentTime: 0,
    duration: 0,
    volume: 0.7,
    queue: [],
    currentIndex: -1,
  },
  togglePlay: vi.fn(),
  next: vi.fn(),
  prev: vi.fn(),
  seek: vi.fn(),
  setVolume: vi.fn(),
}));

vi.mock('../../api/userStore', () => ({
  checkLoginStatus: vi.fn(),
  userStore: {
    isLoggedIn: false,
    nickname: '',
    avatar: '',
  },
}));

vi.mock('../../components/Sidebar.vue', () => ({ default: { template: '<aside />' } }));
vi.mock('../../components/Topbar.vue', () => ({ default: { template: '<header />' } }));
vi.mock('../../components/PlayerBar.vue', () => ({ default: { template: '<footer />' } }));
vi.mock('../../components/Drawer.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../../components/QueuePanel.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../HomeView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../SearchView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../PlaylistView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LyricView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../SettingsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LoginView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../HistoryView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../StatsView.vue', () => ({ default: { template: '<main />' } }));

import App from '../../App.vue';

describe('App network banner', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    backendHealthMock.mockResolvedValue({ ok: true, status: 200, text: '{"status":1}' });
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  it('does not show offline browsing when native health is ok', async () => {
    const wrapper = mount(App);

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    expect(wrapper.text()).not.toContain('网络连接不稳定，已切换离线浏览');
  });
});

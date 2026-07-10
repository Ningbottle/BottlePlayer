import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';

const backendHealthMock = vi.hoisted(() => vi.fn());
const pingMock = vi.hoisted(() => vi.fn());
const transitionEnterMock = vi.hoisted(() => vi.fn((_el, done?: () => void) => done?.()));
const transitionLeaveMock = vi.hoisted(() => vi.fn((_el, done?: () => void) => done?.()));

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
  ping: pingMock,
}));

vi.mock('../../api/motion', () => ({
  transitionEnter: transitionEnterMock,
  transitionLeave: transitionLeaveMock,
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

vi.mock('../../components/Sidebar.vue', () => ({
  default: {
    emits: ['navigate'],
    template: '<aside><button data-test="go-search" @click="$emit(\'navigate\', \'search\')" /></aside>',
  },
}));
vi.mock('../../components/Topbar.vue', () => ({
  default: {
    props: ['searchQuery'],
    emits: ['update:searchQuery'],
    template: '<header><button data-test="edit-search" @click="$emit(\'update:searchQuery\', \'typed\')" /></header>',
  },
}));
vi.mock('../../components/PlayerBar.vue', () => ({ default: { template: '<footer />' } }));
vi.mock('../../components/Drawer.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../../components/QueuePanel.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../HomeView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../SearchView.vue', () => ({ default: { props: ['query'], template: '<main data-test="search-view" />' } }));
vi.mock('../PlaylistView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LyricView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../SettingsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LoginView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../HistoryView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../StatsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../EqualizerView.vue', () => ({ default: { template: '<main />' } }));

import App from '../../App.vue';

describe('App network banner', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    backendHealthMock.mockResolvedValue({ ok: true, status: 200, text: '{"status":1}' });
    pingMock.mockResolvedValue('pong');
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  it('does not show offline browsing when the Tauri shell is reachable', async () => {
    backendHealthMock.mockResolvedValue({ ok: false, status: 0, text: 'request_timeout' });

    const wrapper = mount(App);

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    expect(pingMock).toHaveBeenCalled();
    expect(backendHealthMock).not.toHaveBeenCalled();
    expect(wrapper.text()).not.toContain('网络连接不稳定，已切换离线浏览');
  });

  it('shows a backend banner when the Tauri shell is unreachable', async () => {
    pingMock.mockRejectedValue(new Error('tauri unavailable'));

    const wrapper = mount(App);

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    expect(wrapper.text()).toContain('应用后台连接不稳定，部分功能可能暂不可用');
  });

  it('keeps the backend banner below the titlebar controls', async () => {
    pingMock.mockRejectedValue(new Error('tauri unavailable'));

    const wrapper = mount(App);

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    const appChildren = Array.from(wrapper.find('.app').element.children);
    const titlebarIndex = appChildren.findIndex(el => el.classList.contains('titlebar'));
    const bannerIndex = appChildren.findIndex(el => el.classList.contains('network-banner'));

    expect(titlebarIndex).toBeGreaterThanOrEqual(0);
    expect(bannerIndex).toBeGreaterThan(titlebarIndex);
    expect(wrapper.find('.titlebar-controls .close').exists()).toBe(true);
  });

  it('wraps the main scroll view in JS transition hooks', () => {
    const wrapper = mount(App);

    const transition = wrapper.findComponent({ name: 'Transition' });

    expect(transition.exists()).toBe(true);
    expect(transition.props('css')).toBe(false);
    expect(transition.props('mode')).toBe('out-in');
    expect(transition.props('onEnter')).toBe(transitionEnterMock);
    expect(transition.props('onLeave')).toBe(transitionLeaveMock);
  });

  it('does not remount SearchView while the query input changes', async () => {
    const wrapper = mount(App);

    await wrapper.find('[data-test="go-search"]').trigger('click');
    await nextTick();
    const firstSearchElement = wrapper.find('[data-test="search-view"]').element;

    await wrapper.find('[data-test="edit-search"]').trigger('click');
    await nextTick();

    expect(wrapper.find('[data-test="search-view"]').element).toBe(firstSearchElement);
  });
});

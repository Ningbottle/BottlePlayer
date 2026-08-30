import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';

const backendHealthMock = vi.hoisted(() => vi.fn());
const pingMock = vi.hoisted(() => vi.fn());
const transitionEnterMock = vi.hoisted(() => vi.fn((_el, done?: () => void) => done?.()));
const transitionLeaveMock = vi.hoisted(() => vi.fn((_el, done?: () => void) => done?.()));
const homeFeedStoreMock = vi.hoisted(() => ({
  ensureLoaded: vi.fn(),
  refresh: vi.fn(),
  daily: { loading: false, refreshing: false, error: null, items: [] },
  playlists: { loading: false, refreshing: false, error: null, items: [] },
  albums: { loading: false, refreshing: false, error: null, items: [] },
}));

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

vi.mock('../../platform/tauri/nativeClient', () => ({
  backendHealth: backendHealthMock,
  isCircuitOpen: () => true,
  ping: pingMock,
}));

vi.mock('../../api/motion', () => ({
  transitionEnter: transitionEnterMock,
  transitionLeave: transitionLeaveMock,
  animateElement: vi.fn(() => ({ kill: () => {} })),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  isReducedMotion: vi.fn(() => true),
}));

vi.mock('../../api/homeFeedStore', () => ({
  useHomeFeedStore: () => homeFeedStoreMock,
}));

vi.mock('../../playback/playerStore', () => ({
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
  playPersonalFm: vi.fn(),
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
    template: '<aside><button data-test="go-search" @click="$emit(\'navigate\', \'search\')" /><button data-test="go-stats" @click="$emit(\'navigate\', \'stats\')" /></aside>',
  },
}));
vi.mock('../../components/Topbar.vue', () => ({
  default: {
    props: ['searchQuery'],
    emits: ['update:searchQuery', 'back'],
    template: '<header><button data-test="edit-search" @click="$emit(\'update:searchQuery\', \'typed\')" /><button data-test="go-back" @click="$emit(\'back\')" /></header>',
  },
}));
vi.mock('../../playback/components/PlayerBar.vue', () => ({ default: { template: '<footer />' } }));
vi.mock('../../playback/components/QueuePanel.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../SearchView.vue', () => ({ default: { props: ['query'], template: '<main data-test="search-view" />' } }));
vi.mock('../PlaylistView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LyricView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../SettingsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../LoginView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../HistoryView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../StatsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../EqualizerView.vue', () => ({ default: { template: '<main />' } }));

import App from '../../App.vue';
import { useThemeStore, __resetForTest as resetTheme } from '../../api/themeStore';
import { createAppRouter } from '../../navigation/router';
import { routeNames } from '../../navigation/routes';

async function mountApp() {
  const router = createAppRouter();
  await router.push({ name: routeNames.home });
  await router.isReady();
  return {
    router,
    wrapper: mount(App, { global: { plugins: [router] } }),
  };
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
  await flushPromises();
  await nextTick();
}

describe('App network banner', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    resetTheme();
    homeFeedStoreMock.ensureLoaded.mockReset();
    backendHealthMock.mockResolvedValue({ ok: true, status: 200, text: '{"status":1}' });
    pingMock.mockResolvedValue('pong');
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
    resetTheme();
  });

  it('does not show offline browsing when the Tauri shell is reachable', async () => {
    backendHealthMock.mockResolvedValue({ ok: false, status: 0, text: 'request_timeout' });

    const { wrapper } = await mountApp();

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    expect(pingMock).toHaveBeenCalled();
    expect(backendHealthMock).not.toHaveBeenCalled();
    expect(wrapper.text()).not.toContain('网络连接不稳定，已切换离线浏览');
  });

  it('shows a backend banner when the Tauri shell is unreachable', async () => {
    pingMock.mockRejectedValue(new Error('tauri unavailable'));

    const { wrapper } = await mountApp();

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    expect(wrapper.text()).toContain('网络或服务暂时不可用');
    expect(wrapper.text()).toContain('本地队列与已缓存内容仍可浏览');
  });

  it('keeps the backend banner below the titlebar controls', async () => {
    pingMock.mockRejectedValue(new Error('tauri unavailable'));

    const { wrapper } = await mountApp();

    await vi.advanceTimersByTimeAsync(1_000);
    await flushPromises();

    const appChildren = Array.from(wrapper.find('.app').element.children);
    const titlebarIndex = appChildren.findIndex(el => el.classList.contains('titlebar'));
    const bannerIndex = appChildren.findIndex(el => el.classList.contains('network-banner'));

    expect(titlebarIndex).toBeGreaterThanOrEqual(0);
    expect(bannerIndex).toBeGreaterThan(titlebarIndex);
    expect(wrapper.find('.titlebar-controls .close').exists()).toBe(true);
  });

  it('wraps the main scroll view in JS transition hooks', async () => {
    const { wrapper } = await mountApp();

    const transition = wrapper.findComponent({ name: 'Transition' });

    expect(transition.exists()).toBe(true);
    expect(transition.props('css')).toBe(false);
    // Default skin is Aurora → overlap (no mode / simultaneous enter+leave)
    expect(transition.props('mode')).toBeUndefined();
    expect(transition.props('onEnter')).toBe(transitionEnterMock);
    expect(transition.props('onLeave')).toBe(transitionLeaveMock);
    expect(wrapper.find('.scroll').classes()).toContain('page-transition-stack');
  });

  it('uses out-in page transitions for Newsprint without overlap stack', async () => {
    const { setSkin } = useThemeStore();
    setSkin('newsprint');

    const { wrapper } = await mountApp();
    await nextTick();

    const transition = wrapper.findComponent({ name: 'Transition' });
    expect(transition.props('mode')).toBe('out-in');
    expect(wrapper.find('.scroll').classes()).not.toContain('page-transition-stack');
  });

  it('Aurora overlap stack places transition children in one grid cell (no double height)', async () => {
    // Source contract from App.vue — scoped SFC CSS is not injected under vitest/jsdom.
    const { readFileSync } = await import('node:fs');
    const { resolve } = await import('node:path');
    const appSource = readFileSync(resolve(__dirname, '../../App.vue'), 'utf8');
    expect(appSource).toMatch(/\.scroll\.page-transition-stack\s*\{[\s\S]*?display:\s*grid/);
    expect(appSource).toMatch(/grid-area:\s*1\s*\/\s*1/);
    expect(appSource).toMatch(/page-transition-stack':\s*isAuroraOverlap/);

    const { wrapper } = await mountApp();
    const scroll = wrapper.get('.scroll').element as HTMLElement;
    expect(scroll.classList.contains('page-transition-stack')).toBe(true);

    // Apply the same one-cell grid rules and assert two temporary children share a cell
    // (do not stack as two flex/block rows that double the scroll height).
    const style = document.createElement('style');
    style.textContent = `
      .scroll.page-transition-stack {
        display: grid;
        grid-template-columns: minmax(0, 1fr);
        align-items: start;
      }
      .scroll.page-transition-stack > * {
        grid-area: 1 / 1;
        min-width: 0;
        min-height: 100%;
      }
    `;
    document.head.appendChild(style);

    const leaving = document.createElement('div');
    leaving.dataset.role = 'leaving';
    leaving.style.height = '200px';
    const entering = document.createElement('div');
    entering.dataset.role = 'entering';
    entering.style.height = '200px';
    // Clear any real view nodes so we only measure the two synthetic pages
    scroll.replaceChildren(leaving, entering);

    const leaveStyle = getComputedStyle(leaving);
    const enterStyle = getComputedStyle(entering);
    expect(getComputedStyle(scroll).display).toBe('grid');
    // Same grid area = stacked overlap, not sequential rows
    expect(leaveStyle.gridArea).toBe(enterStyle.gridArea);
    expect(leaveStyle.gridArea === '1 / 1' || leaveStyle.gridArea === '1 / 1 / auto / auto').toBe(true);

    style.remove();
  });

  it('does not remount SearchView while the query input changes', async () => {
    const { wrapper, router } = await mountApp();

    await clickAndWaitForNavigation(router, wrapper, '[data-test="go-search"]');
    const firstSearchElement = wrapper.find('[data-test="search-view"]').element;

    await wrapper.find('[data-test="edit-search"]').trigger('click');
    await nextTick();

    expect(wrapper.find('[data-test="search-view"]').element).toBe(firstSearchElement);
  });

  it('keeps the same HomeView instance when navigating away and back', async () => {
    const { wrapper, router } = await mountApp();
    await flushPromises();

    expect(homeFeedStoreMock.ensureLoaded).toHaveBeenCalledTimes(1);

    const scroll = wrapper.get('.scroll').element as HTMLElement;
    scroll.scrollTop = 146;

    await clickAndWaitForNavigation(router, wrapper, '[data-test="go-stats"]');
    scroll.scrollTop = 0;

    await clickAndWaitForNavigation(router, wrapper, '[data-test="go-back"]');

    expect(homeFeedStoreMock.ensureLoaded).toHaveBeenCalledTimes(1);
    expect(scroll.scrollTop).toBe(146);
  });
});

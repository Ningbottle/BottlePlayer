import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/* ── Shared mocks ── */

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

/* ── Shell-only tests (mount in isolation) ── */

import AuroraShell from '../AuroraShell.vue';
import NewsprintShell from '../NewsprintShell.vue';

const newsprintCss = readFileSync(resolve(__dirname, '../../../styles/skins/newsprint.css'), 'utf8');

describe('AuroraShell', () => {
  it('renders the BottleMusic wordmark', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.get('[data-test="shell-brand"]').text()).toContain('BottleMusic');
  });
  it('renders data-shell="aurora"', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.find('[data-shell="aurora"]').exists()).toBe(true);
  });

  it('uses <nav> for sidebar landmark', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.find('nav.shell-sidebar').exists()).toBe(true);
    expect(wrapper.find('aside.shell-sidebar').exists()).toBe(false);
  });

  it('uses <main> for content landmark', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.find('main.shell-main').exists()).toBe(true);
    expect(wrapper.find('article.shell-main').exists()).toBe(false);
  });

  it('uses <footer> for playerbar landmark', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.find('footer.shell-playerbar').exists()).toBe(true);
  });

  it('does not render paper background layers', () => {
    const wrapper = mount(AuroraShell);
    expect(wrapper.find('.paper-base').exists()).toBe(false);
    expect(wrapper.find('.paper-fibers').exists()).toBe(false);
    expect(wrapper.find('.paper-grain').exists()).toBe(false);
    expect(wrapper.find('.paper-vignette').exists()).toBe(false);
  });

  it('renders titlebar with data-tauri-drag-region', () => {
    const wrapper = mount(AuroraShell);
    const titlebar = wrapper.find('.titlebar');
    expect(titlebar.exists()).toBe(true);
    expect(titlebar.attributes('data-tauri-drag-region')).toBeDefined();
  });

  it('renders all required slots', () => {
    const wrapper = mount(AuroraShell, {
      slots: {
        'titlebar-center': '<div class="t-tc">tc</div>',
        banner: '<div class="t-bn">bn</div>',
        sidebar: '<div class="t-sb">sb</div>',
        topbar: '<div class="t-tb">tb</div>',
        default: '<div class="t-df">df</div>',
        extras: '<div class="t-ex">ex</div>',
        playerbar: '<div class="t-pb">pb</div>',
      },
    });
    expect(wrapper.find('.t-tc').exists()).toBe(true);
    expect(wrapper.find('.t-bn').exists()).toBe(true);
    expect(wrapper.find('.t-sb').exists()).toBe(true);
    expect(wrapper.find('.t-tb').exists()).toBe(true);
    expect(wrapper.find('.t-df').exists()).toBe(true);
    expect(wrapper.find('.t-ex').exists()).toBe(true);
    expect(wrapper.find('.t-pb').exists()).toBe(true);
  });

  it('identifies the modern immersive Aurora layout without changing landmarks', () => {
    const wrapper = mount(AuroraShell);
    const root = wrapper.get('[data-shell="aurora"]');

    expect(root.attributes('data-layout')).toBe('immersive');
    expect(wrapper.get('.titlebar-logo').text()).toContain('BottleMusic');
    expect(wrapper.find('nav.shell-sidebar').exists()).toBe(true);
    expect(wrapper.find('main.shell-main').exists()).toBe(true);
    expect(wrapper.find('footer.shell-playerbar').exists()).toBe(true);
  });
});

describe('NewsprintShell', () => {
  it('renders the BottleMusic masthead', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.get('[data-test="shell-brand"]').text()).toContain('BottleMusic');
    expect(wrapper.text()).not.toContain('The Player');
  });
  it('renders data-shell="newsprint"', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.find('[data-shell="newsprint"]').exists()).toBe(true);
  });

  it('collapses every shell chrome row in lyric fullscreen mode', () => {
    expect(newsprintCss).toMatch(
      /\.app\[data-shell="newsprint"\]\.lyric-fullscreen-active\s*\{[\s\S]*?grid-template-rows:\s*0 0 1fr 0\s*!important;/,
    );
    expect(newsprintCss).toMatch(
      /\.app\[data-shell="newsprint"\]\.lyric-fullscreen-active \.titlebar[\s\S]*?display:\s*none\s*!important;/,
    );
    expect(newsprintCss).toMatch(
      /\.app\[data-shell="newsprint"\]\.lyric-fullscreen-active \.shell-main[\s\S]*?grid-row:\s*1 \/ -1;/,
    );
  });

  it('uses <aside> for sidebar landmark', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.find('aside.shell-sidebar').exists()).toBe(true);
    expect(wrapper.find('nav.shell-sidebar').exists()).toBe(false);
  });

  it('uses <article> for content landmark', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.find('article.shell-main').exists()).toBe(true);
    expect(wrapper.find('main.shell-main').exists()).toBe(false);
  });

  it('uses <section> for playerbar landmark', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.find('section.shell-playerbar').exists()).toBe(true);
  });

  it('renders paper background layers', () => {
    const wrapper = mount(NewsprintShell);
    expect(wrapper.find('.paper-base').exists()).toBe(true);
    expect(wrapper.find('.paper-fibers').exists()).toBe(true);
    expect(wrapper.find('.paper-grain').exists()).toBe(true);
    expect(wrapper.find('.paper-vignette').exists()).toBe(true);
  });

  it('renders titlebar with data-tauri-drag-region', () => {
    const wrapper = mount(NewsprintShell);
    const titlebar = wrapper.find('.titlebar');
    expect(titlebar.exists()).toBe(true);
    expect(titlebar.attributes('data-tauri-drag-region')).toBeDefined();
  });

  it('renders all required slots', () => {
    const wrapper = mount(NewsprintShell, {
      slots: {
        'titlebar-center': '<div class="t-tc">tc</div>',
        banner: '<div class="t-bn">bn</div>',
        sidebar: '<div class="t-sb">sb</div>',
        topbar: '<div class="t-tb">tb</div>',
        default: '<div class="t-df">df</div>',
        extras: '<div class="t-ex">ex</div>',
        playerbar: '<div class="t-pb">pb</div>',
      },
    });
    expect(wrapper.find('.t-tc').exists()).toBe(true);
    expect(wrapper.find('.t-bn').exists()).toBe(true);
    expect(wrapper.find('.t-sb').exists()).toBe(true);
    expect(wrapper.find('.t-tb').exists()).toBe(true);
    expect(wrapper.find('.t-df').exists()).toBe(true);
    expect(wrapper.find('.t-ex').exists()).toBe(true);
    expect(wrapper.find('.t-pb').exists()).toBe(true);
  });
});

describe('Shell DOM hierarchy differences', () => {
  it('sidebar landmark differs: Aurora <nav> vs Newsprint <aside>', () => {
    const aurora = mount(AuroraShell);
    const newsprint = mount(NewsprintShell);
    expect(aurora.find('nav.shell-sidebar').exists()).toBe(true);
    expect(aurora.find('aside.shell-sidebar').exists()).toBe(false);
    expect(newsprint.find('aside.shell-sidebar').exists()).toBe(true);
    expect(newsprint.find('nav.shell-sidebar').exists()).toBe(false);
  });

  it('content landmark differs: Aurora <main> vs Newsprint <article>', () => {
    const aurora = mount(AuroraShell);
    const newsprint = mount(NewsprintShell);
    expect(aurora.find('main.shell-main').exists()).toBe(true);
    expect(aurora.find('article.shell-main').exists()).toBe(false);
    expect(newsprint.find('article.shell-main').exists()).toBe(true);
    expect(newsprint.find('main.shell-main').exists()).toBe(false);
  });

  it('playerbar landmark differs: Aurora <footer> vs Newsprint <section>', () => {
    const aurora = mount(AuroraShell);
    const newsprint = mount(NewsprintShell);
    expect(aurora.find('footer.shell-playerbar').exists()).toBe(true);
    expect(aurora.find('section.shell-playerbar').exists()).toBe(false);
    expect(newsprint.find('section.shell-playerbar').exists()).toBe(true);
    expect(newsprint.find('footer.shell-playerbar').exists()).toBe(false);
  });

  it('titlebar landmark differs: Aurora <div> vs Newsprint <header>', () => {
    const aurora = mount(AuroraShell);
    const newsprint = mount(NewsprintShell);
    expect(aurora.find('div.titlebar').exists()).toBe(true);
    expect(aurora.find('header.titlebar').exists()).toBe(false);
    expect(newsprint.find('header.titlebar').exists()).toBe(true);
    expect(newsprint.find('div.titlebar').exists()).toBe(false);
  });

  it('Newsprint has paper background, Aurora does not', () => {
    const aurora = mount(AuroraShell);
    const newsprint = mount(NewsprintShell);
    expect(newsprint.find('.paper-base').exists()).toBe(true);
    expect(aurora.find('.paper-base').exists()).toBe(false);
  });
});

describe('Window controls', () => {
  it('AuroraShell renders exactly minimize, maximize, close buttons', () => {
    const wrapper = mount(AuroraShell);
    const controls = wrapper.find('.titlebar-controls');
    expect(controls.exists()).toBe(true);
    const buttons = controls.findAll('button');
    expect(buttons).toHaveLength(3);
    expect(buttons[0].classes()).toContain('min');
    expect(buttons[1].classes()).toContain('max');
    expect(buttons[2].classes()).toContain('close');
  });

  it('NewsprintShell renders exactly minimize, maximize, close buttons', () => {
    const wrapper = mount(NewsprintShell);
    const controls = wrapper.find('.titlebar-controls');
    expect(controls.exists()).toBe(true);
    const buttons = controls.findAll('button');
    expect(buttons).toHaveLength(3);
    expect(buttons[0].classes()).toContain('min');
    expect(buttons[1].classes()).toContain('max');
    expect(buttons[2].classes()).toContain('close');
  });

  it('buttons have accessible aria-labels', () => {
    const wrapper = mount(AuroraShell);
    const controls = wrapper.find('.titlebar-controls');
    expect(controls.find('.min').attributes('aria-label')).toBeTruthy();
    expect(controls.find('.max').attributes('aria-label')).toBeTruthy();
    expect(controls.find('.close').attributes('aria-label')).toBeTruthy();
  });
});

/* ── State preservation test (mounts App with mocked children) ── */

const pingMock = vi.hoisted(() => vi.fn());
const transitionEnterMock = vi.hoisted(() => vi.fn((_el: Element, done?: () => void) => done?.()));
const transitionLeaveMock = vi.hoisted(() => vi.fn((_el: Element, done?: () => void) => done?.()));
const homeFeedStoreMock = vi.hoisted(() => ({
  ensureLoaded: vi.fn(),
  refresh: vi.fn(),
  daily: { loading: false, refreshing: false, error: null, items: [] },
  playlists: { loading: false, refreshing: false, error: null, items: [] },
  albums: { loading: false, refreshing: false, error: null, items: [] },
}));

vi.mock('../../../api/backend', () => ({
  backendHealth: vi.fn(),
  isCircuitOpen: () => false,
  ping: pingMock,
}));

vi.mock('../../../api/motion', () => ({
  transitionEnter: transitionEnterMock,
  transitionLeave: transitionLeaveMock,
  animateElement: vi.fn(() => ({ kill: () => {} })),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startVinylSpin: vi.fn(() => ({ kill: () => {}, setPlaying: () => {}, burst: () => {} })),
  isReducedMotion: vi.fn(() => true),
}));

vi.mock('../../../api/coverFlight', () => ({
  flyCoverToDock: vi.fn(),
}));

vi.mock('../../../api/homeFeedStore', () => ({
  useHomeFeedStore: () => homeFeedStoreMock,
}));

vi.mock('../../../api/playerStore', () => ({
  initPlayer: vi.fn(),
  initPlayerBackend: vi.fn(),
  playerStore: {
    currentTrack: { name: 'Test Song', singer: 'Test Artist' } as any,
    isPlaying: false,
    currentTime: 42,
    duration: 180,
    volume: 0.7,
    queue: [{ name: 'Q1' }, { name: 'Q2' }] as any,
    currentIndex: 0,
  },
  togglePlay: vi.fn(),
  next: vi.fn(),
  prev: vi.fn(),
  seek: vi.fn(),
  setVolume: vi.fn(),
  setQuality: vi.fn(),
  playPersonalFm: vi.fn(),
}));

vi.mock('../../../api/userStore', () => ({
  checkLoginStatus: vi.fn(),
  userStore: {
    isLoggedIn: false,
    nickname: '',
    avatar: '',
  },
}));

vi.mock('../../../components/Sidebar.vue', () => ({
  default: {
    emits: ['navigate'],
    template: '<aside><button data-test="go-search" @click="$emit(\'navigate\', \'search\')" /></aside>',
  },
}));
vi.mock('../../../components/Topbar.vue', () => ({
  default: {
    props: ['searchQuery'],
    emits: ['update:searchQuery', 'back'],
    template: '<header><button data-test="edit-search" @click="$emit(\'update:searchQuery\', \'typed\')" /></header>',
  },
}));
vi.mock('../../../components/PlayerBar.vue', () => ({ default: { template: '<footer />' } }));
vi.mock('../../../components/QueuePanel.vue', () => ({ default: { template: '<div />' } }));
vi.mock('../../../views/SearchView.vue', () => ({ default: { props: ['query'], template: '<main data-test="search-view" />' } }));
vi.mock('../../../views/PlaylistView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/LyricView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/SettingsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/LoginView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/HistoryView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/StatsView.vue', () => ({ default: { template: '<main />' } }));
vi.mock('../../../views/EqualizerView.vue', () => ({ default: { template: '<main />' } }));

import { useThemeStore, __resetForTest } from '../../../api/themeStore';
import App from '../../../App.vue';
import { createAppRouter } from '../../../navigation/router';
import { routeNames } from '../../../navigation/routes';

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

describe('Skin switch preserves state', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    __resetForTest();
    pingMock.mockResolvedValue('pong');
    homeFeedStoreMock.ensureLoaded.mockReset();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
  });

  it('preserves currentView, searchQuery across skinId switch', async () => {
    const { wrapper, router } = await mountApp();
    await flushPromises();

    // Navigate to search view
    await clickAndWaitForNavigation(router, wrapper, '[data-test="go-search"]');
    expect(wrapper.find('[data-test="search-view"]').exists()).toBe(true);

    // Switch skin from aurora to newsprint
    const theme = useThemeStore();
    expect(theme.skinId.value).toBe('aurora');
    theme.setSkin('newsprint');
    await nextTick();
    await flushPromises();

    // currentView should still be search
    expect(wrapper.find('[data-test="search-view"]').exists()).toBe(true);

    // Switch back to aurora
    theme.setSkin('aurora');
    await nextTick();
    await flushPromises();

    // Still on search view
    expect(wrapper.find('[data-test="search-view"]').exists()).toBe(true);
  });
});

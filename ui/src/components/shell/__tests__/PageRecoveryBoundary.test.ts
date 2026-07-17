import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { defineComponent, h, nextTick, reactive, ref, type Component } from 'vue';
import { mount, flushPromises } from '@vue/test-utils';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const route = reactive({ fullPath: '/broken-page?from=test' });
const router = { replace: vi.fn() };
const cancelPageTransitionMock = vi.hoisted(() => vi.fn());
const mountedWrappers: Array<{ unmount: () => void }> = [];

vi.mock('vue-router', () => ({
  useRoute: () => route,
  useRouter: () => router,
}));

vi.mock('../../../navigation/navigationLifecycle', () => ({
  cancelPageTransition: cancelPageTransitionMock,
}));

import PageRecoveryBoundary from '../PageRecoveryBoundary.vue';
import { lyricFullscreen, setLyricFullscreen } from '../../../api/lyricFullscreen';
import { useThemeStore, __resetForTest as resetTheme } from '../../../api/themeStore';
import { beginTransitionSession } from '../../../api/transitionSession';

function mountHarness(page: Component) {
  const broken = ref(false);
  const retryKeys: number[] = [];
  const wrapper = mount(defineComponent({
    setup() {
      return () => h('div', { class: 'harness' }, [
        h('aside', { 'data-test': 'shell' }, 'Shell'),
        h(PageRecoveryBoundary, null, {
          default: ({ retryKey }: { retryKey: number }) => {
            retryKeys.push(retryKey);
            return h(page, {
              key: retryKey,
              broken: broken.value,
              routePath: route.fullPath,
            });
          },
        }),
        h('footer', { 'data-test': 'player' }, 'Player'),
      ]);
    },
  }), {
    global: {
      config: {
        errorHandler: () => {},
      },
    },
  });
  mountedWrappers.push(wrapper);
  return {
    wrapper,
    broken,
    retryKeys,
    fail: () => { broken.value = true; },
  };
}

function deferred<T>() {
  let resolve!: (value: T | PromiseLike<T>) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((promiseResolve, promiseReject) => {
    resolve = promiseResolve;
    reject = promiseReject;
  });
  return { promise, resolve, reject };
}

describe('PageRecoveryBoundary', () => {
  beforeEach(() => {
    router.replace.mockReset();
    router.replace.mockResolvedValue(undefined);
    cancelPageTransitionMock.mockClear();
    route.fullPath = '/broken-page?from=test';
    resetTheme();
    setLyricFullscreen(true);
  });

  afterEach(() => {
    mountedWrappers.forEach((wrapper) => wrapper.unmount());
    mountedWrappers.length = 0;
    setLyricFullscreen(false);
    vi.restoreAllMocks();
  });

  it('keeps shell and player visible while replacing a failed page with Chinese recovery UI', async () => {
    const error = new Error('raw page failure');
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        return () => {
          if (props.broken) throw error;
          return h('main', 'unreachable');
        };
      },
    });
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    await nextTick();

    expect(wrapper.get('[data-test="shell"]').text()).toBe('Shell');
    expect(wrapper.get('[data-test="player"]').text()).toBe('Player');
    expect(wrapper.text()).toContain('页面暂时无法显示');
    expect(wrapper.text()).not.toContain(error.message);
    expect(lyricFullscreen.value).toBe(false);
    expect(cancelPageTransitionMock).toHaveBeenCalledTimes(1);
    expect(consoleError).toHaveBeenCalledWith(
      '[PageRecoveryBoundary]',
      expect.objectContaining({
        routeFullPath: route.fullPath,
        skin: 'aurora',
        error,
      }),
    );
  });

  it('returns home through the router and clears the failure state', async () => {
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        const attempts = ref(0);
        attempts.value++;
        return () => {
          if (props.broken && attempts.value === 1) throw new Error('broken route');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, broken } = mountHarness(page);
    router.replace.mockImplementation(async () => {
      broken.value = false;
    });
    broken.value = true;
    await flushPromises();
    const homeButton = wrapper.findAll('button').find((button) => button.text().trim() === '返回首页');
    expect(homeButton).toBeDefined();
    await homeButton!.trigger('click');
    await nextTick();

    expect(router.replace).toHaveBeenCalledWith({ name: 'home' });
    expect(wrapper.text()).not.toContain('页面暂时无法显示');
  });

  it('keeps recovery mounted while returning home is pending, then clears after success', async () => {
    const attempts = ref(0);
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        attempts.value++;
        return () => {
          if (props.broken && attempts.value === 1) throw new Error('pending home failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const navigation = deferred<void>();
    router.replace.mockReturnValue(navigation.promise);
    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    const homeButton = wrapper.findAll('button').find((button) => button.text().trim() === '返回首页');
    expect(homeButton).toBeDefined();
    await homeButton!.trigger('click');
    await nextTick();

    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);
    navigation.resolve();
    await flushPromises();
    await nextTick();
    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(false);
  });

  it('keeps recovery mounted when returning home is rejected', async () => {
    const attempts = ref(0);
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        attempts.value++;
        return () => {
          if (props.broken && attempts.value === 1) throw new Error('rejected home failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const navigationError = new Error('home navigation rejected');
    router.replace.mockRejectedValue(navigationError);
    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    const homeButton = wrapper.findAll('button').find((button) => button.text().trim() === '返回首页');
    expect(homeButton).toBeDefined();
    await homeButton!.trigger('click');
    await flushPromises();
    await nextTick();

    expect(router.replace).toHaveBeenCalledWith({ name: 'home' });
    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);
  });

  it('keeps recovery mounted when router.replace resolves with a navigation failure', async () => {
    const attempts = ref(0);
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        attempts.value++;
        return () => {
          if (props.broken && attempts.value === 1) throw new Error('aborted home failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    router.replace.mockResolvedValue({ type: 4, to: '/home', from: route.fullPath });
    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    const homeButton = wrapper.findAll('button').find((button) => button.text().trim() === '返回首页');
    expect(homeButton).toBeDefined();
    await homeButton!.trigger('click');
    await flushPromises();
    await nextTick();

    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);
  });

  it('clears a stale failure when the reactive route fullPath changes', async () => {
    const page = defineComponent({
      props: { broken: Boolean, routePath: String },
      setup(props) {
        return () => {
          if (props.broken && props.routePath === '/broken-page?from=test') {
            throw new Error('stale route failure');
          }
          return h('main', { 'data-test': 'routed-page' }, props.routePath);
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);

    route.fullPath = '/next-page';
    await nextTick();
    await flushPromises();

    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(false);
    expect(wrapper.get('[data-test="routed-page"]').text()).toBe('/next-page');
  });

  it('does not let an older navigation completion clear a newer route failure', async () => {
    const thrownPaths = new Set<string>();
    const page = defineComponent({
      props: { broken: Boolean, routePath: String },
      setup(props) {
        return () => {
          if (props.broken && !thrownPaths.has(props.routePath ?? '')) {
            thrownPaths.add(props.routePath ?? '');
            throw new Error(`failure for ${props.routePath}`);
          }
          return h('main', { 'data-test': 'routed-page' }, props.routePath);
        };
      },
    });
    const consoleError = vi.spyOn(console, 'error').mockImplementation(() => {});
    const navigation = deferred<void>();
    router.replace.mockReturnValue(navigation.promise);

    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();
    const homeButton = wrapper.findAll('button').find((button) => button.text().trim() === '返回首页');
    expect(homeButton).toBeDefined();
    await homeButton!.trigger('click');

    route.fullPath = '/new-failure-page';
    await nextTick();
    await flushPromises();
    expect(consoleError).toHaveBeenCalledTimes(2);
    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);

    navigation.resolve();
    await flushPromises();
    await nextTick();

    expect(wrapper.find('[data-testid="page-recovery"]').exists()).toBe(true);
    expect(wrapper.get('[data-testid="page-recovery"]').text()).toContain('页面暂时无法显示');
  });

  it('retries with a new retry key and rebuilds the failed page', async () => {
    const attempts = ref(0);
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        attempts.value++;
        return () => {
          if (props.broken && attempts.value === 1) throw new Error('first render failure');
          return h('main', { 'data-test': 'recovered-page' }, `attempt ${attempts.value}`);
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, retryKeys, fail } = mountHarness(page);
    fail();
    await flushPromises();
    const retryButton = wrapper.findAll('button').find((button) => button.text().trim() === '重试当前页面');
    expect(retryButton).toBeDefined();
    await retryButton!.trigger('click');
    await nextTick();

    expect(router.replace).not.toHaveBeenCalled();
    expect(retryKeys).toContain(0);
    expect(retryKeys[retryKeys.length - 1]).toBe(1);
    expect(attempts.value).toBe(2);
    expect(wrapper.get('[data-test="recovered-page"]').text()).toBe('attempt 2');
  });

  it('exposes keyboard-operable native buttons for both recovery actions', async () => {
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        return () => {
          if (props.broken) throw new Error('keyboard failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();

    const buttons = wrapper.findAll('button');
    expect(buttons).toHaveLength(2);
    expect(buttons.map((button) => button.attributes('type'))).toEqual(['button', 'button']);
    expect(buttons.map((button) => button.text())).toEqual(['返回首页', '重试当前页面']);
    expect(buttons.every((button) => button.element.tabIndex >= 0)).toBe(true);
  });

  it('renders one token-driven structure for both skins', async () => {
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        return () => {
          if (props.broken) throw new Error('skin failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});
    const { setSkin } = useThemeStore();

    const auroraMount = mountHarness(page);
    auroraMount.fail();
    const auroraWrapper = auroraMount.wrapper;
    await flushPromises();
    const auroraRecovery = auroraWrapper.find('[data-testid="page-recovery"]');
    expect(auroraRecovery.exists()).toBe(true);
    const auroraMarkup = auroraRecovery.exists() ? auroraRecovery.html() : '';
    auroraWrapper.unmount();

    setSkin('newsprint');
    const newsprintMount = mountHarness(page);
    newsprintMount.fail();
    const newsprintWrapper = newsprintMount.wrapper;
    await flushPromises();
    const recovery = newsprintWrapper.find('[data-testid="page-recovery"]');
    expect(recovery.exists()).toBe(true);
    if (!recovery.exists()) return;
    expect(recovery.attributes('data-skin')).toBe('newsprint');
    expect(recovery.html()).toBe(auroraMarkup.replace('data-skin="aurora"', 'data-skin="newsprint"'));
  });

  it('keeps the recovery state unframed', () => {
    const styleSource = readFileSync(resolve(__dirname, '../../../style.css'), 'utf8');
    const panelRule = styleSource.match(/\.page-recovery__panel\s*\{([\s\S]*?)\}/)?.[1] ?? '';
    expect(panelRule).toMatch(/width/);
    expect(panelRule).not.toMatch(/background|box-shadow|border|border-radius/);
  });

  it('uses a stable route identity together with retryKey for the routed child', () => {
    const appSource = readFileSync(resolve(__dirname, '../../../App.vue'), 'utf8');
    const routedChild = appSource.match(/<component\s+:is="Component"[\s\S]*?\/>/)?.[0] ?? '';
    expect(routedChild).toContain(':key="`${String(route.name)}:${retryKey}`"');
  });

  it('settles active transition sessions and restores their original inline styles', async () => {
    const element = document.createElement('div');
    element.style.opacity = '0.7';
    element.style.transform = 'scale(1)';
    element.style.filter = 'none';
    document.body.appendChild(element);
    const done = vi.fn();
    beginTransitionSession(element, 'enter', done);
    element.style.opacity = '0';
    element.style.transform = 'translateY(28px)';
    element.style.filter = 'blur(2px)';
    const page = defineComponent({
      props: { broken: Boolean },
      setup(props) {
        return () => {
          if (props.broken) throw new Error('transition failure');
          return h('main');
        };
      },
    });
    vi.spyOn(console, 'error').mockImplementation(() => {});

    const { wrapper, fail } = mountHarness(page);
    fail();
    await flushPromises();

    expect(done).toHaveBeenCalledTimes(1);
    expect(element.style.opacity).toBe('0.7');
    expect(element.style.transform).toBe('scale(1)');
    expect(element.style.filter).toBe('none');
    element.remove();
    wrapper.unmount();
  });
});

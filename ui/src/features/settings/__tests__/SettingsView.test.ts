import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));
vi.mock('../../../platform/tauri/updater', () => ({
  checkForUpdate: vi.fn().mockResolvedValue(null),
  relaunchApp: vi.fn().mockResolvedValue(undefined),
  openExternalUrl: vi.fn().mockResolvedValue(undefined),
}));
const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
  apiPost: (...args: any[]) => mockApiPost(...args),
}));
vi.mock('../../account', async (importOriginal) => {
  const actual = await importOriginal<Record<string, unknown>>();
  return {
    ...actual,
    checkLoginStatus: vi.fn().mockResolvedValue(undefined),
    ensureVipDeviceReady: vi.fn().mockResolvedValue({ ok: true }),
    formatVipClaimFailure: vi.fn((result: any) =>
      result?.error_code ? `领取失败：酷狗返回错误码 ${result.error_code}` : '领取失败：酷狗未返回具体原因'),
  };
});
vi.mock('../../../app/update/skippedVersion', () => ({ setSkippedVersion: vi.fn() }));

import SettingsView from '../SettingsView.vue';
import { checkForUpdate, openExternalUrl, type UpdateDownloadEvent } from '../../../platform/tauri/updater';
import { playbackDiagnostics } from '../../../playback/playbackDiagnostics';
import { useAppearanceStore, __resetForTest as resetAppearance } from '../../../app/appearance/appearanceStore';
import { __resetForTest as resetTheme } from '../../../app/appearance/themeStore';
import { logoutLocal, userStore } from '../../account';

// Reduced-motion stub: enter completes synchronously; leave queues completion
// in a microtask so Vue's out-in patch can finish first. This keeps transitions
// deterministic in jsdom, where the non-reduced GSAP tweens would otherwise hang.
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
});
afterEach(() => {
  vi.unstubAllGlobals();
});

// Pre-C7b the progress callback read `event.data.chunkLength` directly, so a
// malformed Progress event (no data) threw inside downloadAndInstall and the
// UI surfaced 下载失败. The optional-chaining added in C7b silently treats it
// as +0 bytes; this contract locks the original fail-loud behavior.
describe('SettingsView update download failure semantics', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    resetAppearance();
    resetTheme();
  });

  async function mountUpdateSection() {
    const wrapper = mount(SettingsView);
    await flushPromises();
    const nav = wrapper.findAll('[data-test="settings-nav-item"]');
    // The update nav item is the one labelled 更新.
    const updateNav = nav.find((n) => n.text().includes('更新'))!;
    await updateNav.trigger('click');
    await flushPromises();
    return wrapper;
  }

  it('surfaces 下载失败 instead of silently completing on a malformed Progress event', async () => {
    vi.mocked(checkForUpdate).mockImplementation(async () => ({
      version: '9.9.9',
      body: '',
      async downloadAndInstall(handler?: (e: UpdateDownloadEvent) => void) {
        // Malformed stream: Progress carries no data at all.
        const h = handler as (e: unknown) => void;
        h({ event: 'Started', data: { contentLength: 1000 } });
        h({ event: 'Progress' });
        h({ event: 'Finished' });
      },
    }));

    const wrapper = await mountUpdateSection();

    // 检查更新 populates updateVersion → the 下载并安装 button appears.
    const checkBtn = wrapper.findAll('button').find((b) => b.text().includes('检查更新'))!;
    await checkBtn.trigger('click');
    await flushPromises();
    expect(wrapper.text()).toContain('发现新版本 v9.9.9');

    const downloadBtn = wrapper.findAll('button').find((b) => b.text().includes('下载并安装'))!;
    await downloadBtn.trigger('click');
    await flushPromises();

    // The malformed Progress must surface a failure, not complete silently.
    expect(wrapper.text()).toContain('下载失败');
    expect(wrapper.text()).not.toContain('✓ 更新已安装');
    wrapper.unmount();
  });
});

describe('SettingsView playback diagnostics', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    playbackDiagnostics.reset();
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    playbackDiagnostics.reset();
  });

  it('renders playback diagnostics events (most-recent-first) with a working copy button', async () => {
    playbackDiagnostics.recordEvent({ kind: 'track_switch', phase: 'start', detail: 'switched to h1', trackKey: 'h1' });
    playbackDiagnostics.recordEvent({ kind: 'potential_stall', phase: 'noop', detail: 'no activity for 5s' });

    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.assign(navigator, { clipboard: { writeText } });

    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    // Sub-nav design: diagnostics is not the default section — switch to it.
    const diagNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('诊断'));
    await diagNav!.trigger('click');
    await flushPromises();

    const section = wrapper.get('[data-test="playback-diagnostics"]');
    // most-recent-first: potential_stall recorded last → appears first
    expect(section.text()).toContain('potential_stall');
    expect(section.text()).toContain('no activity for 5s');
    expect(section.text()).toContain('track_switch');
    // potential_stall row is highlighted
    expect(section.find('.diag-stall').exists()).toBe(true);

    await wrapper.find('[data-test="copy-diagnostics"]').trigger('click');
    expect(writeText).toHaveBeenCalled();
    const copiedText = writeText.mock.calls[0][0];
    expect(copiedText).toContain('track_switch');
    expect(copiedText).toContain('potential_stall');
  });
});

describe('SettingsView sub-navigation', () => {
  let wrapper: VueWrapper<any> | undefined;
  beforeEach(() => {
    localStorage.clear();
    resetTheme();
    logoutLocal();
    userStore.isLoggedIn = true;
    userStore.userId = 'settings-vip-test-user';
    userStore.deviceReady = false;
    userStore.isVip = false;
    userStore.vipStatus = 'unknown';
    userStore.vipEndDate = '';
    userStore.observedEntitlements = [];
    userStore.claimMessage = '';
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
    mockApiPost.mockReset();
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: true, dfid: 'abcdefghijklmnopqrstuvwx' },
    });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    logoutLocal();
    vi.useRealTimers();
  });

  it('renders a sub-nav with 6 items and shows only the active section', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const navItems = wrapper.findAll('[data-test="settings-nav-item"]');
    expect(navItems).toHaveLength(6);
    // Default section is "appearance" — only it is visible.
    expect(wrapper.find('[data-test="settings-section-appearance"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="settings-section-diagnostics"]').exists()).toBe(false);
  });

  it('switches to the diagnostics section when its nav item is clicked', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const diagNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('诊断'));
    await diagNav!.trigger('click');
    await flushPromises();
    expect(wrapper.find('[data-test="settings-section-diagnostics"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="settings-section-appearance"]').exists()).toBe(false);
  });

  it('Appearance section calls appearanceStore.setSkin when a skin is selected', async () => {
    const store = useAppearanceStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const newsprintBtn = wrapper.find('[data-test="select-skin-newsprint"]');
    await newsprintBtn.trigger('click');
    await flushPromises();
    expect(store.skin.value).toBe('newsprint');
    expect(document.documentElement.dataset.skin).toBe('newsprint');
  });

  it('Appearance section calls appearanceStore.setMode when dark mode is toggled', async () => {
    const store = useAppearanceStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const darkBtn = wrapper.find('[data-test="select-mode-dark"]');
    await darkBtn.trigger('click');
    await flushPromises();
    expect(store.mode.value).toBe('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });

  it('shows the upstream VIP error code instead of an official-app guess', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();

    const claimButton = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'));
    await claimButton!.trigger('click');
    await flushPromises();

    const vipSection = wrapper.get('[data-test="settings-section-vip"]');
    expect(vipSection.text()).toContain('51002');
    expect(vipSection.text()).not.toContain('需要酷狗官方 App 内领取');
  });

  it('does not call a status=1 listen claim successful when authority reports no active VIP', async () => {
    userStore.isVip = false;
    userStore.vipStatus = 'unknown';
    userStore.vipEndDate = '';
    userStore.observedEntitlements = [];
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') return { status: 1 };
      if (path === '/user/vip/detail') {
        return { status: 1, authoritative: true, data: { is_vip: 0, vip_type: 0, busi_vip: [] } };
      }
      return { status: 1, data: {} };
    });

    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();
    await wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'))!.trigger('click');
    await flushPromises();

    const message = wrapper.get('[data-test="settings-section-vip"]').text();
    expect(message).toContain('未生效');
    expect(message).not.toContain('✓ 听歌领 VIP 成功');
    expect(mockApiGet).toHaveBeenCalledWith('/user/vip/detail');
  });

  it('reports listen VIP activation only after authoritative VIP confirmation', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') return { status: 1 };
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2999-01-01 00:00:00' },
        };
      }
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    await wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'))!.trigger('click');
    await flushPromises();

    const message = wrapper.get('[data-test="settings-section-vip"]').text();
    expect(message).toContain('✓ 本次领取已确认生效');
    expect(message).toContain('2999-01-01 00:00:00');
    expect(message).not.toContain('✓ 听歌领 VIP 成功');
  });

  it('never reports an expired VIP snapshot as a successful ad claim', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') return { status: 1 };
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2020-01-01 00:00:00' },
        };
      }
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    await wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'))!.trigger('click');
    await flushPromises();

    const message = wrapper.get('[data-test="settings-section-vip"]').text();
    expect(message).toContain('已过期');
    expect(message).toContain('未生效');
    expect(message).not.toContain('✓ 本次领取已确认生效');
  });

  it('keeps a pre-existing active VIP separate from this claim', async () => {
    userStore.isVip = true;
    userStore.vipStatus = 'active';
    userStore.vipEndDate = '2999-01-01 00:00:00';
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') return { status: 1 };
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2999-01-01 00:00:00' },
        };
      }
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    await wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'))!.trigger('click');
    await flushPromises();

    const message = wrapper.get('[data-test="settings-section-vip"]').text();
    expect(message).toContain('领取已受理');
    expect(message).toContain('当前音乐 VIP 仍有效');
    expect(message).toContain('尚未确认本次领取新增或延期权益');
    expect(message).not.toContain('✓ 本次领取已确认生效');
  });

  it('shows accepted-but-unconfirmed after authoritative VIP refresh failures', async () => {
    vi.useFakeTimers();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') return { status: 1 };
      if (path === '/user/vip/detail') throw new Error('vip detail timeout');
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    const click = wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'))!.trigger('click');
    await flushPromises();
    await vi.runAllTimersAsync();
    await click;
    await flushPromises();

    const message = wrapper.get('[data-test="settings-section-vip"]').text();
    expect(message).toContain('权益状态未确认');
    expect(message).not.toContain('✓ 本次领取已确认生效');
    expect(mockApiGet.mock.calls.filter(([path]) => path === '/user/vip/detail')).toHaveLength(4);
  });

  it('drops a late same-account claim message after logout and relogin', async () => {
    let resolveClaim!: (value: unknown) => void;
    const pendingClaim = new Promise((resolve) => { resolveClaim = resolve; });
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/youth/listen/song') return pendingClaim;
      return Promise.resolve({ status: 1, data: {} });
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    const click = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'))!.trigger('click');
    await flushPromises();
    expect(mockApiGet).toHaveBeenCalledWith('/youth/listen/song');

    logoutLocal();
    userStore.isLoggedIn = true;
    userStore.userId = 'settings-vip-test-user';
    await flushPromises();
    resolveClaim({ status: 1 });
    await click;
    await flushPromises();

    expect(wrapper.get('[data-test="settings-section-vip"]').text()).not.toContain('✓');
    expect(mockApiGet).not.toHaveBeenCalledWith('/user/vip/detail');
    expect(userStore.claimMessage).toBe('');
  });

  it('does not write a late confirmation message into an unmounted Settings view', async () => {
    let resolveVip!: (value: unknown) => void;
    const pendingVip = new Promise((resolve) => { resolveVip = resolve; });
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/youth/listen/song') return Promise.resolve({ status: 1 });
      if (path === '/user/vip/detail') return pendingVip;
      return Promise.resolve({ status: 1, data: {} });
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    await wrapper.findAll('[data-test="settings-nav-item"]')
      .find((node) => node.text() === 'VIP')!.trigger('click');
    await flushPromises();
    const click = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'))!.trigger('click');
    await flushPromises();
    const state = wrapper.vm.$.setupState as { listenVipLoading: boolean; listenVipMsg: string };
    expect(mockApiGet).toHaveBeenCalledWith('/user/vip/detail');
    expect(wrapper.get('[data-test="settings-section-vip"]').text()).toContain('领取已受理，正在核验权益');
    expect(wrapper.get('[data-test="settings-section-vip"]').text()).not.toContain('✓');

    wrapper.unmount();
    wrapper = undefined;
    expect(state.listenVipLoading).toBe(false);
    resolveVip({ status: 1, authoritative: true, data: { is_vip: 0, vip_type: 0, busi_vip: [] } });
    await click;
    await flushPromises();

    expect(state.listenVipMsg).toBe('');
    expect(state.listenVipLoading).toBe(false);
  });

  it('registers the device before listen and ad VIP requests, then confirms each route', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();

    const listenButton = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'));
    await listenButton!.trigger('click');
    await flushPromises();
    expect(mockApiPost).toHaveBeenCalledWith('/register/dev');
    expect(mockApiGet).toHaveBeenCalledWith('/youth/listen/song');
    expect(mockApiGet).toHaveBeenCalledWith('/user/vip/detail');
    expect(mockApiPost.mock.invocationCallOrder[0]).toBeLessThan(
      mockApiGet.mock.invocationCallOrder.find((_, index) => mockApiGet.mock.calls[index][0] === '/youth/listen/song')!,
    );

    mockApiGet.mockClear();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
    mockApiPost.mockClear();
    const adButton = wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'));
    await adButton!.trigger('click');
    await flushPromises();
    expect(mockApiPost).toHaveBeenCalledWith('/register/dev');
    expect(mockApiGet).toHaveBeenCalledWith('/youth/vip/ad');
    expect(mockApiGet).toHaveBeenCalledWith('/user/vip/detail');
  });

  it('does not send listen or ad activity requests when device registration fails', async () => {
    mockApiPost.mockResolvedValue({ status: 0, error: 'device_registration_failed' });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();

    mockApiGet.mockClear();
    const listenButton = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'));
    await listenButton!.trigger('click');
    await flushPromises();
    expect(mockApiGet).not.toHaveBeenCalledWith('/youth/listen/song');
    expect(wrapper.get('[data-test="settings-section-vip"]').text()).toContain('设备注册失败');

    mockApiGet.mockClear();
    const adButton = wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'));
    await adButton!.trigger('click');
    await flushPromises();
    expect(mockApiGet).not.toHaveBeenCalledWith('/youth/vip/ad');
    expect(wrapper.get('[data-test="settings-section-vip"]').text()).toContain('设备注册失败');
  });

  it('does not guess that ad VIP must be claimed in the official app', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        return { status: 0, error_code: 51003, error_msg: '' };
      }
      return { status: 1, data: {} };
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();

    const claimButton = wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'));
    await claimButton!.trigger('click');
    await flushPromises();

    const vipSection = wrapper.get('[data-test="settings-section-vip"]');
    expect(vipSection.text()).toContain('51003');
    expect(vipSection.text()).not.toContain('需要酷狗官方 App 内领取');
  });
});

describe('SettingsView appearance controls', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    localStorage.clear();
    resetAppearance();
    resetTheme();
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('shows the allowed appearance controls and excludes unrelated controls', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    const section = wrapper.get('[data-test="settings-section-appearance"]');
    expect(section.text()).toContain('极光');
    expect(section.text()).toContain('Aurora');
    expect(section.text()).toContain('报刊');
    expect(section.text()).toContain('Newsprint');
    expect(section.text()).toContain('强调色');
    expect(section.text()).toContain('Accent');
    expect(section.text()).toContain('紧凑列表');
    expect(section.text()).toContain('Compact List');
    expect(section.text()).toContain('歌词对齐');
    expect(section.text()).toContain('Lyric Alignment');
    expect(section.find('[data-test="settings-accent-input"]').attributes('type')).toBe('color');
    expect(section.find('[data-test="settings-compact-list"]').attributes('type')).toBe('checkbox');
    expect(section.find('[data-test="settings-lyric-align-left"]').exists()).toBe(true);
    expect(section.find('[data-test="settings-lyric-align-center"]').exists()).toBe(true);
    expect(section.find('[data-test="settings-lyric-focus"]').exists()).toBe(false);
    expect(section.text()).not.toMatch(/字体|背景|暖|模糊|噪|grain|blur|cache|缓存/i);
  });

  it('labels skin and mode groups and exposes pressed states with secondary English', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    const skinGroup = wrapper.get('[data-test="settings-skin-group"]');
    expect(skinGroup.attributes('role')).toBe('group');
    expect(skinGroup.attributes('aria-labelledby')).toBe('settings-skin-label');
    expect(skinGroup.get('#settings-skin-label').text()).toContain('皮肤');
    expect(skinGroup.get('#settings-skin-label .settings-control-secondary').text()).toBe('Skin');
    expect(skinGroup.get('[data-test="select-skin-aurora"]').attributes('aria-pressed')).toBe('true');
    expect(skinGroup.get('[data-test="select-skin-newsprint"]').attributes('aria-pressed')).toBe('false');
    expect(skinGroup.get('[data-test="select-skin-aurora"] .settings-control-secondary').text()).toBe('Aurora');
    expect(skinGroup.get('[data-test="select-skin-newsprint"] .settings-control-secondary').text()).toBe('Newsprint');

    const modeGroup = wrapper.get('[data-test="settings-mode-group"]');
    expect(modeGroup.attributes('role')).toBe('group');
    expect(modeGroup.attributes('aria-labelledby')).toBe('settings-mode-label');
    expect(modeGroup.get('#settings-mode-label').text()).toContain('光感');
    expect(modeGroup.get('#settings-mode-label .settings-control-secondary').text()).toBe('Mode');
    expect(modeGroup.get('[data-test="select-mode-light"]').attributes('aria-pressed')).toBe('true');
    expect(modeGroup.get('[data-test="select-mode-dark"]').attributes('aria-pressed')).toBe('false');
    expect(modeGroup.get('[data-test="select-mode-light"] .settings-control-secondary').text()).toBe('Light');
    expect(modeGroup.get('[data-test="select-mode-dark"] .settings-control-secondary').text()).toBe('Dark');

    expect(wrapper.get('label[for="settings-accent"]').text()).toContain('强调色');
    expect(wrapper.get('[data-test="settings-appearance-compact-list"]').element.tagName).toBe('LABEL');
    expect(wrapper.get('[data-test="settings-appearance-compact-list"]').text()).toContain('紧凑列表');
    expect(wrapper.get('[data-test="settings-appearance-lyric-align"] [role="group"]').attributes('aria-labelledby')).toBe(
      'settings-lyric-align-label',
    );
  });

  it('persists and reflects accent, compact-list, and lyric alignment changes', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    await wrapper.get('[data-test="settings-accent-input"]').setValue('#ff0000');
    await wrapper.get('[data-test="settings-compact-list"]').setValue(true);
    await wrapper.get('[data-test="settings-lyric-align-center"]').trigger('click');
    await flushPromises();

    const store = useAppearanceStore();
    expect(store.accent.value).toBe('#ff0000');
    expect(store.compactList.value).toBe(true);
    expect(store.lyricAlign.value).toBe('center');
    expect(localStorage.getItem('appearance_accent')).toBe('#ff0000');
    expect(localStorage.getItem('appearance_compact_list')).toBe('true');
    expect(localStorage.getItem('appearance_lyric_align')).toBe('center');
    expect(wrapper.get('[data-test="settings-lyric-align-center"]').attributes('aria-pressed')).toBe('true');
  });

  it('shows a retry action after a failed appearance write and persists through the real store on retry', async () => {
    const setItem = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new DOMException('full', 'QuotaExceededError');
    });
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    await wrapper.get('[data-test="select-skin-newsprint"]').trigger('click');
    await flushPromises();

    const status = wrapper.get('[data-test="appearance-storage-status"]');
    expect(status.text()).toContain('本地设置暂时无法读取或保存');
    expect(status.text()).toContain('当前界面可继续使用');
    expect(useAppearanceStore().skin.value).toBe('newsprint');
    expect(localStorage.getItem('appearance_skin')).toBeNull();

    setItem.mockRestore();
    await wrapper.get('[data-test="retry-appearance-storage"]').trigger('click');
    await flushPromises();

    expect(localStorage.getItem('appearance_skin')).toBe('newsprint');
    expect(wrapper.find('[data-test="appearance-storage-status"]').exists()).toBe(false);
    expect(useAppearanceStore().storageDegraded.value).toBe(false);
  });

  it('opens the device-help site through the scoped system opener', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();

    const deviceNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text().includes('设备'));
    await deviceNav!.trigger('click');
    await flushPromises();
    await wrapper.get('[data-test="open-device-help"]').trigger('click');

    expect(openExternalUrl).toHaveBeenCalledWith('https://m.kugou.com/');
  });
});

describe('SettingsView storage and diagnostics preservation', () => {
  let wrapper: VueWrapper<any> | undefined;

  beforeEach(() => {
    localStorage.clear();
    resetAppearance();
    resetTheme();
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('storage section does not show a fake cache confirmation modal', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const storageNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('存储'));
    await storageNav!.trigger('click');
    await flushPromises();
    expect(wrapper.text()).not.toContain('确认清理');
    expect(wrapper.text()).not.toContain('清理本地数据缓存');
  });

  it('storage section still renders with SQLite3 description', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const storageNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('存储'));
    await storageNav!.trigger('click');
    await flushPromises();
    expect(wrapper.find('[data-test="settings-section-storage"]').exists()).toBe(true);
    expect(wrapper.text()).toContain('SQLite3');
  });

  it('diagnostics section still renders', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const diagNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('诊断'));
    await diagNav!.trigger('click');
    await flushPromises();
    expect(wrapper.find('[data-test="settings-section-diagnostics"]').exists()).toBe(true);
  });
});

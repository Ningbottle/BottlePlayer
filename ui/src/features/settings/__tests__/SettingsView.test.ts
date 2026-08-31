import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));
vi.mock('../../../platform/tauri/updater', () => ({
  checkForUpdate: vi.fn().mockResolvedValue(null),
  relaunchApp: vi.fn().mockResolvedValue(undefined),
  openExternalUrl: vi.fn().mockResolvedValue(undefined),
}));
const mockApiGet = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));
vi.mock('../../account', () => ({
  checkLoginStatus: vi.fn().mockResolvedValue(undefined),
  ensureVipDeviceReady: vi.fn().mockResolvedValue({ ok: true }),
  formatVipClaimFailure: vi.fn((result: any) =>
    result?.error_code ? `领取失败：酷狗返回错误码 ${result.error_code}` : '领取失败：酷狗未返回具体原因'),
}));
vi.mock('../../../app/update/skippedVersion', () => ({ setSkippedVersion: vi.fn() }));

import SettingsView from '../SettingsView.vue';
import { checkForUpdate, openExternalUrl, type UpdateDownloadEvent } from '../../../platform/tauri/updater';
import { playbackDiagnostics } from '../../../playback/playbackDiagnostics';
import { useAppearanceStore, __resetForTest as resetAppearance } from '../../../app/appearance/appearanceStore';
import { __resetForTest as resetTheme } from '../../../app/appearance/themeStore';
import { ensureVipDeviceReady } from '../../account';

// Reduced-motion stub: makes GSAP transition hooks (transitionEnter/Leave)
// call done() synchronously so <Transition> leave/enter completes within the
// test tick. Without this, jsdom has no matchMedia and gsap tweens would hang.
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
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
    vi.mocked(ensureVipDeviceReady).mockReset();
    vi.mocked(ensureVipDeviceReady).mockResolvedValue({ ok: true });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
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

  it('calls ensureVipDeviceReady before listen and ad VIP requests', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const vipNav = wrapper.findAll('[data-test="settings-nav-item"]').find((node) => node.text() === 'VIP');
    await vipNav!.trigger('click');
    await flushPromises();

    const listenButton = wrapper.findAll('button').find((button) => button.text().includes('听歌领 VIP'));
    await listenButton!.trigger('click');
    await flushPromises();
    expect(ensureVipDeviceReady).toHaveBeenCalled();
    expect(mockApiGet).toHaveBeenCalledWith('/youth/listen/song');

    vi.mocked(ensureVipDeviceReady).mockClear();
    mockApiGet.mockClear();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
    const adButton = wrapper.findAll('button').find((button) => button.text().includes('看广告领 VIP'));
    await adButton!.trigger('click');
    await flushPromises();
    expect(ensureVipDeviceReady).toHaveBeenCalled();
    expect(mockApiGet).toHaveBeenCalledWith('/youth/vip/ad');
  });

  it('does not send listen or ad activity requests when device registration fails', async () => {
    vi.mocked(ensureVipDeviceReady).mockResolvedValue({
      ok: false,
      error: 'device_registration_failed',
    });
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

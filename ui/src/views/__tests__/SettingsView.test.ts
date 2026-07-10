import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(undefined) }));
vi.mock('@tauri-apps/plugin-updater', () => ({ check: vi.fn().mockResolvedValue(null) }));
vi.mock('@tauri-apps/plugin-process', () => ({ relaunch: vi.fn() }));
const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({ apiGet: (...args: any[]) => mockApiGet(...args) }));
vi.mock('../../api/userStore', () => ({ checkLoginStatus: vi.fn().mockResolvedValue(undefined) }));
vi.mock('../../api/skippedVersion', () => ({ setSkippedVersion: vi.fn() }));

import SettingsView from '../SettingsView.vue';
import { playbackDiagnostics } from '../../api/playbackDiagnostics';
import { useThemeStore, __resetForTest as resetTheme } from '../../api/themeStore';

// Reduced-motion stub: makes GSAP transition hooks (transitionEnter/Leave)
// call done() synchronously so <Transition> leave/enter completes within the
// test tick. Without this, jsdom has no matchMedia and gsap tweens would hang.
beforeEach(() => {
  vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
});
afterEach(() => {
  vi.unstubAllGlobals();
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

  it('Appearance section calls themeStore.setSkin when a skin is selected', async () => {
    const store = useThemeStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const newsprintBtn = wrapper.find('[data-test="select-skin-newsprint"]');
    await newsprintBtn.trigger('click');
    await flushPromises();
    expect(store.skinId.value).toBe('newsprint');
    expect(document.documentElement.dataset.skin).toBe('newsprint');
  });

  it('Appearance section calls themeStore.setMode when dark mode is toggled', async () => {
    const store = useThemeStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const darkBtn = wrapper.find('[data-test="select-mode-dark"]');
    await darkBtn.trigger('click');
    await flushPromises();
    expect(store.mode.value).toBe('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });
});

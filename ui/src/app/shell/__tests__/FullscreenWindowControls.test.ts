import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount } from '@vue/test-utils';

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    minimize: vi.fn(),
  }),
}));

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

import FullscreenWindowControls from '../FullscreenWindowControls.vue';
import { lyricFullscreen, setLyricFullscreen } from '../../../features/lyrics';

describe('FullscreenWindowControls', () => {
  beforeEach(() => {
    setLyricFullscreen(true);
  });

  afterEach(() => {
    setLyricFullscreen(false);
  });

  it('renders exactly 2 buttons by default', () => {
    const wrapper = mount(FullscreenWindowControls);
    expect(wrapper.findAll('button')).toHaveLength(2);
    expect(wrapper.text().trim()).toBe('');
  });

  it('can hide minimize or exit independently', () => {
    const minOnly = mount(FullscreenWindowControls, {
      props: { showMinimize: true, showExit: false },
    });
    expect(minOnly.find('[data-test="fs-minimize"]').exists()).toBe(true);
    expect(minOnly.find('[data-test="fs-exit-fullscreen"]').exists()).toBe(false);

    const exitOnly = mount(FullscreenWindowControls, {
      props: { showMinimize: false, showExit: true },
    });
    expect(exitOnly.find('[data-test="fs-minimize"]').exists()).toBe(false);
    expect(exitOnly.find('[data-test="fs-exit-fullscreen"]').exists()).toBe(true);
  });

  it('has a minimize button with accessible label', () => {
    const wrapper = mount(FullscreenWindowControls);
    const min = wrapper.find('[data-test="fs-minimize"]');
    expect(min.exists()).toBe(true);
    expect(min.attributes('aria-label')).toBeTruthy();
  });

  it('has an exit-fullscreen button with accessible label', () => {
    const wrapper = mount(FullscreenWindowControls);
    const exit = wrapper.find('[data-test="fs-exit-fullscreen"]');
    expect(exit.exists()).toBe(true);
    expect(exit.attributes('aria-label')).toBeTruthy();
  });

  it('does not render a maximize button', () => {
    const wrapper = mount(FullscreenWindowControls);
    expect(wrapper.find('[data-test="fs-maximize"]').exists()).toBe(false);
    expect(wrapper.find('.max').exists()).toBe(false);
  });

  it('does not render a close button', () => {
    const wrapper = mount(FullscreenWindowControls);
    expect(wrapper.find('[data-test="fs-close"]').exists()).toBe(false);
    expect(wrapper.find('.close').exists()).toBe(false);
  });

  it('exit-fullscreen button sets lyricFullscreen to false', async () => {
    const wrapper = mount(FullscreenWindowControls);
    await wrapper.find('[data-test="fs-exit-fullscreen"]').trigger('click');
    expect(lyricFullscreen.value).toBe(false);
  });

  it('keeps the button-only overlay out of the native drag region', () => {
    const wrapper = mount(FullscreenWindowControls);
    const container = wrapper.find('[data-test="fs-controls"]');
    expect(container.exists()).toBe(true);
    expect(container.attributes('data-tauri-drag-region')).toBeUndefined();
    expect(wrapper.findAll('button').every((button) => (
      button.attributes('data-tauri-drag-region') === 'false'
    ))).toBe(true);
  });
});

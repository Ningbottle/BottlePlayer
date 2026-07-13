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
import { lyricFullscreen, setLyricFullscreen } from '../../../api/lyricFullscreen';

describe('FullscreenWindowControls', () => {
  beforeEach(() => {
    setLyricFullscreen(true);
  });

  afterEach(() => {
    setLyricFullscreen(false);
  });

  it('renders exactly 2 buttons', () => {
    const wrapper = mount(FullscreenWindowControls);
    expect(wrapper.findAll('button')).toHaveLength(2);
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

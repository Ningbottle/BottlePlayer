import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import EqualizerPanel from '../EqualizerPanel.vue';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

import { invoke } from '@tauri-apps/api/core';
import { playerStore } from '../../api/playerStore';

describe('EqualizerPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.backend = 'native';
    playerStore.eqEnabled = true;
    playerStore.eqBands = [0, 0, 0, 0, 0];
    playerStore.activePreset = 'Flat';
  });

  it('renders 5 band sliders when expanded', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const sliders = wrapper.findAll('input[type="range"]');
    expect(sliders.length).toBe(5);
  });

  it('slider change calls invoke playback_set_eq_bands', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const slider = wrapper.find('input[type="range"]');
    await slider.setValue('6');
    await nextTick();
    expect(invoke).toHaveBeenCalledWith('playback_set_eq_bands', {
      gains: expect.arrayContaining([6, 0, 0, 0, 0]),
    });
  });

  it('applies preset when dropdown changes', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const select = wrapper.find('select');
    await select.setValue('Bass Boost');
    await nextTick();
    expect(playerStore.eqBands).toEqual([6, 4, 0, 0, 0]);
    expect(invoke).toHaveBeenCalledWith('playback_set_eq_bands', {
      gains: [6, 4, 0, 0, 0],
    });
  });

  it('shows hint when backend is html5', async () => {
    playerStore.backend = 'html5';
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    expect(wrapper.text()).toContain('Native backend not available');
  });
});

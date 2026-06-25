import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import EqualizerPanel from '../EqualizerPanel.vue';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

vi.mock('../../api/playerStore', async () => {
  const actual = await vi.importActual<typeof import('../../api/playerStore')>('../../api/playerStore');
  return {
    ...actual,
    setWebAudioEqBand: vi.fn(),
    setWebAudioEqEnabled: vi.fn(),
  };
});

import { playerStore, setWebAudioEqBand, setWebAudioEqEnabled } from '../../api/playerStore';

describe('EqualizerPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.backend = 'html5';
    playerStore.eqEnabled = true;
    playerStore.eqBands = [0, 0, 0, 0, 0];
    playerStore.activePreset = 'Flat';
  });

  it('renders 5 band sliders when expanded', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const sliders = wrapper.findAll('input[type="range"]');
    expect(sliders.length).toBe(5);
  });

  it('slider change calls setWebAudioEqBand', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const slider = wrapper.find('input[type="range"]');
    await slider.setValue('6');
    await nextTick();
    expect(setWebAudioEqBand).toHaveBeenCalledWith(0, 6);
  });

  it('applies preset when dropdown changes', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const select = wrapper.find('select');
    await select.setValue('Bass Boost');
    await nextTick();
    expect(playerStore.eqBands).toEqual([6, 4, 0, 0, 0]);
    expect(setWebAudioEqBand).toHaveBeenCalledWith(0, 6);
    expect(setWebAudioEqBand).toHaveBeenCalledWith(1, 4);
  });

  it('toggle enable calls setWebAudioEqEnabled', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const checkbox = wrapper.find('input[type="checkbox"]');
    await checkbox.trigger('change');
    await nextTick();
    expect(setWebAudioEqEnabled).toHaveBeenCalledWith(false);
  });
});

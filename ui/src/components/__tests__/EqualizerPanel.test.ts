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

import { playerStore, setWebAudioEqBand, setWebAudioEqEnabled, eqState } from '../../api/playerStore';

describe('EqualizerPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.backend = 'html5';
    playerStore.eqEnabled = true;
    playerStore.eqBands = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    playerStore.activePreset = 'Flat';
    eqState.available = true;
    eqState.reason = '';
  });

  it('renders the 10 reference EQ bands when expanded', () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const sliders = wrapper.findAll('input[type="range"]');
    expect(sliders).toHaveLength(10);
    expect(wrapper.text()).toContain('31');
    expect(wrapper.text()).toContain('62');
    expect(wrapper.text()).toContain('125');
    expect(wrapper.text()).toContain('250');
    expect(wrapper.text()).toContain('500');
    expect(wrapper.text()).toContain('1K');
    expect(wrapper.text()).toContain('2K');
    expect(wrapper.text()).toContain('4K');
    expect(wrapper.text()).toContain('8K');
    expect(wrapper.text()).toContain('16K');
    expect(sliders[0].attributes('min')).toBe('-6');
    expect(sliders[0].attributes('max')).toBe('6');
  });

  it('slider change calls setWebAudioEqBand', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const slider = wrapper.find('input[type="range"]');
    await slider.setValue('6');
    await nextTick();
    expect(setWebAudioEqBand).toHaveBeenCalledWith(0, 6);
  });

  it('applies the Harman Kardon preset', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const select = wrapper.find('select');
    await select.setValue('Harman Kardon');
    await nextTick();
    expect(playerStore.activePreset).toBe('Harman Kardon');
    expect(playerStore.eqBands).toEqual([2, 3, 2, 0, -1, 0, 1, 2, 2, 1]);
    expect(setWebAudioEqBand).toHaveBeenCalledWith(0, 2);
    expect(setWebAudioEqBand).toHaveBeenCalledWith(9, 1);
  });

  it('resets custom gains to flat', async () => {
    playerStore.eqBands = [0, 0, 6, 0, 0, 0, 0, 0, 0, 0];
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    await wrapper.get('[data-test="eq-reset"]').trigger('click');
    await nextTick();
    expect(playerStore.activePreset).toBe('Flat');
    expect(playerStore.eqBands).toEqual([0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  });

  it('toggle enable calls setWebAudioEqEnabled', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const checkbox = wrapper.find('input[type="checkbox"]');
    await checkbox.trigger('change');
    await nextTick();
    expect(setWebAudioEqEnabled).toHaveBeenCalledWith(false);
  });

  it('shows degradation notice and disables sliders when EQ unavailable', async () => {
    eqState.available = false;
    eqState.reason = '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    expect(wrapper.text()).toContain('EQ 暂不可用');
    const sliders = wrapper.findAll('input[type="range"]');
    expect(sliders.length).toBe(10);
    expect(sliders[0].attributes('disabled')).toBeDefined();
  });
});

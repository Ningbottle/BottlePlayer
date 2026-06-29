import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import EqualizerView from '../EqualizerView.vue';

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

import { playerStore, eqState } from '../../api/playerStore';

describe('EqualizerView', () => {
  beforeEach(() => {
    playerStore.eqEnabled = true;
    playerStore.eqBands = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    playerStore.activePreset = 'Flat';
    eqState.available = false;
    eqState.reason = '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';
  });

  it('renders a full equalizer page with translated bands and sound effects below', () => {
    const wrapper = mount(EqualizerView);

    expect(wrapper.text()).toContain('均衡器');
    expect(wrapper.text()).toContain('Equalizer');
    expect(wrapper.text()).toContain('31Hz · 超低频');
    expect(wrapper.text()).toContain('62Hz · 低频');
    expect(wrapper.text()).toContain('125Hz · 厚度');
    expect(wrapper.text()).toContain('250Hz · 温暖');
    expect(wrapper.text()).toContain('500Hz · 中低频');
    expect(wrapper.text()).toContain('1K · 人声');
    expect(wrapper.text()).toContain('2K · 清晰度');
    expect(wrapper.text()).toContain('4K · 存在感');
    expect(wrapper.text()).toContain('8K · 明亮');
    expect(wrapper.text()).toContain('16K · 空气感');

    const effectSection = wrapper.get('[data-test="sound-effects"]');
    expect(effectSection.text()).toContain('音效预设');
    expect(effectSection.text()).toContain('Sound Effects');
    expect(effectSection.text()).toContain('哈曼卡顿');
    expect(effectSection.text()).toContain('Harman Kardon');
    expect(wrapper.findAll('input[type="range"]')).toHaveLength(10);
  });
});

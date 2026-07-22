import { describe, it, expect, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';
import VisualizerView from '../VisualizerView.vue';
import type { Track } from '../../api/normalizer';

vi.mock('../../api/playerStore', () => ({
  playerStore: reactive({
    audio: null,
    isPlaying: false,
    currentTrack: null as Track | null,
  }),
}));

vi.mock('../../api/motion', () => ({
  isReducedMotion: vi.fn(() => true),
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
}));

describe('VisualizerView', () => {
  it('renders canvas, disc, and an idle prompt without a track', () => {
    const wrapper = mount(VisualizerView);

    wrapper.get('[data-test="visualizer-canvas"]');
    wrapper.get('[data-test="visualizer-disc"]');
    expect(wrapper.get('[data-test="visualizer-idle"]').text()).toContain('播放一首歌');
    wrapper.unmount();
  });

  it('shows track meta when a track is loaded', async () => {
    const { playerStore } = await import('../../api/playerStore');
    // Minimal Track fixture — the mock store accepts the same shape as production.
    const track = {
      FileHash: 'h1',
      SongName: '光谱',
      SingerName: '测试歌手',
      Duration: 100,
    } as Track;
    playerStore.currentTrack = track;

    const wrapper = mount(VisualizerView);

    expect(wrapper.text()).toContain('光谱');
    expect(wrapper.text()).toContain('测试歌手');
    expect(wrapper.find('[data-test="visualizer-idle"]').exists()).toBe(false);
    wrapper.unmount();
    playerStore.currentTrack = null;
  });
});

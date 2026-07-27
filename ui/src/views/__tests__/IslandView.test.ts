import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { reactive } from 'vue';

const syncState = reactive({
  cb: null as null | ((s: Record<string, unknown>) => void),
});

vi.mock('../../api/playerSync', () => ({
  onPlayerState: vi.fn(async (cb: (s: Record<string, unknown>) => void) => {
    syncState.cb = cb;
    return () => {};
  }),
  sendPlayerCommand: vi.fn(async () => {}),
}));

vi.mock('../../api/overlayWindows', () => ({
  isTauriRuntime: () => false,
  settleCurrentOverlay: vi.fn(async () => {}),
  moveCurrentOverlayTo: vi.fn(async () => {}),
}));

vi.mock('../../api/motion', () => ({
  isReducedMotion: vi.fn(() => true),
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(() => ({ close: vi.fn(async () => {}) })),
}));

import IslandView from '../overlay/IslandView.vue';
import { sendPlayerCommand } from '../../api/playerSync';

function emitState(partial: Record<string, unknown> = {}) {
  syncState.cb?.({
    hash: 'h1',
    name: '灵动曲',
    artist: '测试歌手',
    cover: 'http://img.example/c.jpg',
    isPlaying: true,
    currentTime: 30,
    duration: 180,
    volume: 0.8,
    loopMode: 'list',
    ...partial,
  });
}

describe('IslandView', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    syncState.cb = null;
  });

  it('renders idle capsule without a track', () => {
    const wrapper = mount(IslandView);
    expect(wrapper.text()).toContain('未播放');
    expect(wrapper.find('.island-capsule').classes()).toContain('is-idle');
    wrapper.unmount();
  });

  it('shows synced track meta and updates the progress ring', async () => {
    const wrapper = mount(IslandView);
    emitState();
    await wrapper.vm.$nextTick();

    expect(wrapper.text()).toContain('灵动曲');
    expect(wrapper.text()).toContain('测试歌手');
    const img = wrapper.find('.island-disc img');
    expect(img.exists()).toBe(true);

    const ring = wrapper.find('.island-ring-fill');
    // 30/180 → offset = LEN * (1 - 1/6)
    const expected = String(2 * Math.PI * 30 * (1 - 30 / 180));
    expect(Number(ring.attributes('stroke-dashoffset'))).toBeCloseTo(Number(expected), 3);
    wrapper.unmount();
  });

  it('sends transport commands to the main window', async () => {
    const wrapper = mount(IslandView);
    emitState();
    await wrapper.vm.$nextTick();

    const buttons = wrapper.findAll('.island-transport button');
    await buttons[0].trigger('click');
    expect(sendPlayerCommand).toHaveBeenLastCalledWith({ action: 'prev' });
    await buttons[1].trigger('click');
    expect(sendPlayerCommand).toHaveBeenLastCalledWith({ action: 'toggle' });
    await buttons[2].trigger('click');
    expect(sendPlayerCommand).toHaveBeenLastCalledWith({ action: 'next' });
    wrapper.unmount();
  });

  it('disables transport without a track', () => {
    const wrapper = mount(IslandView);
    const buttons = wrapper.findAll('.island-transport button');
    expect(buttons.every((b) => b.attributes('disabled') !== undefined)).toBe(true);
    wrapper.unmount();
  });
});

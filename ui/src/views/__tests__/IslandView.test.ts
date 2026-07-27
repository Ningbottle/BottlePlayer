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
  applySyncedTheme: vi.fn((s: { skin?: string; mode?: string; accent?: string }) => {
    if (s.skin) document.documentElement.dataset.skin = s.skin;
    if (s.mode) document.documentElement.dataset.mode = s.mode;
    if (s.accent) document.documentElement.style.setProperty('--accent', s.accent);
  }),
}));

vi.mock('../../api/overlayWindows', () => ({
  isTauriRuntime: vi.fn(() => false),
  settleCurrentOverlay: vi.fn(async () => {}),
  moveCurrentOverlayTo: vi.fn(async () => {}),
}));

vi.mock('../../api/motion', () => ({
  isReducedMotion: vi.fn(() => true),
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
}));

const windowMock = vi.hoisted(() => ({
  close: vi.fn(async () => {}),
  outerPosition: vi.fn(async () => ({ x: 0, y: 0 })),
  outerSize: vi.fn(async () => ({ width: 340, height: 88 })),
  setSize: vi.fn(async (_size: { width: number; height: number }) => {}),
  setPosition: vi.fn(async (_pos: { x: number; y: number }) => {}),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(() => windowMock),
}));

vi.mock('@tauri-apps/api/dpi', () => ({
  LogicalSize: class {
    constructor(public width: number, public height: number) {}
  },
  LogicalPosition: class {
    constructor(public x: number, public y: number) {}
  },
  PhysicalPosition: class {
    constructor(public x: number, public y: number) {}
  },
}));

import IslandView from '../overlay/IslandView.vue';
import { sendPlayerCommand } from '../../api/playerSync';
import { isTauriRuntime } from '../../api/overlayWindows';
import type { Mock } from 'vitest';

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
    // 30/180 → offset = LEN * (1 - 1/6), ring radius 20
    const expected = String(2 * Math.PI * 20 * (1 - 30 / 180));
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

  it('applies the synced theme to the overlay document', async () => {
    const wrapper = mount(IslandView);
    emitState({ skin: 'aurora', mode: 'dark', accent: '#c4391e' });
    await wrapper.vm.$nextTick();

    expect(document.documentElement.dataset.skin).toBe('aurora');
    expect(document.documentElement.dataset.mode).toBe('dark');
    expect(document.documentElement.style.getPropertyValue('--accent')).toBe('#c4391e');
    wrapper.unmount();
    document.documentElement.style.removeProperty('--accent');
  });

  it('disables transport without a track', () => {
    const wrapper = mount(IslandView);
    const buttons = wrapper.findAll('.island-transport button');
    expect(buttons.every((b) => b.attributes('disabled') !== undefined)).toBe(true);
    wrapper.unmount();
  });

  it('expands to the wide card on a blank click and collapses on Escape', async () => {
    const wrapper = mount(IslandView);
    emitState();
    await wrapper.vm.$nextTick();

    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);

    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 101, clientY: 51 }));
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(true);

    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    wrapper.unmount();
  });

  it('does not expand after a drag (pointer travel >= 5px)', async () => {
    const wrapper = mount(IslandView);
    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 130, clientY: 80 }));
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    wrapper.unmount();
  });

  it('resizes the window growing downward from the pill (tauri)', async () => {
    (isTauriRuntime as Mock).mockReturnValue(true);
    windowMock.outerPosition.mockResolvedValue({ x: 100, y: 100 });
    windowMock.outerSize.mockResolvedValue({ width: 300, height: 64 });

    const wrapper = mount(IslandView);
    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));

    await vi.waitFor(() => {
      expect(windowMock.setSize).toHaveBeenCalledTimes(1);
    });
    const size = windowMock.setSize.mock.calls[0][0] as { width: number; height: number };
    expect(size).toMatchObject({ width: 480, height: 200 });
    const pos = windowMock.setPosition.mock.calls[0][0] as { x: number; y: number };
    expect(pos).toMatchObject({
      x: Math.round(100 + 150 - 240),
      y: 100, // top edge unchanged — the card grows downward
    });
    wrapper.unmount();
  });
});

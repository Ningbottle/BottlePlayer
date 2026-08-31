import { describe, it, expect, vi, beforeEach } from 'vitest';
import { flushPromises, mount } from '@vue/test-utils';
import { reactive } from 'vue';
import overlayCapability from '../../../../src-tauri/capabilities/default.json';

const syncState = reactive({
  cb: null as null | ((s: Record<string, unknown>) => void),
});

vi.mock('../../../playback/sync/playerSync', () => ({
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

vi.mock('../../../platform/tauri/windows', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../../../platform/tauri/windows')>();
  return {
    ...actual,
    isTauriRuntime: vi.fn(() => false),
    settleCurrentOverlay: vi.fn(async () => {}),
    moveCurrentOverlayTo: vi.fn(async () => {}),
  };
});

vi.mock('../../../shared/motion/motion', () => ({
  isReducedMotion: vi.fn(() => true),
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
}));

const windowMock = vi.hoisted(() => ({
  close: vi.fn(async () => {}),
  outerPosition: vi.fn(async () => ({ x: 0, y: 0 })),
  outerSize: vi.fn(async () => ({ width: 340, height: 88 })),
  scaleFactor: vi.fn(async () => 1),
  setSize: vi.fn(async (_size: { width: number; height: number }) => {}),
  setPosition: vi.fn(async (_pos: { x: number; y: number }) => {}),
  setBackgroundColor: vi.fn(async (_color: [number, number, number, number]) => {}),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(() => windowMock),
}));

const webviewMock = vi.hoisted(() => ({
  setBackgroundColor: vi.fn(async (_color: [number, number, number, number]) => {}),
}));

vi.mock('@tauri-apps/api/webview', () => ({
  getCurrentWebview: vi.fn(() => webviewMock),
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

import IslandView from '../IslandView.vue';
import { sendPlayerCommand } from '../../../playback/sync/playerSync';
import { isTauriRuntime, settleCurrentOverlay } from '../../../platform/tauri/windows';
import { startVinylSpin } from '../../../shared/motion/motion';
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
    vi.useRealTimers();
    vi.clearAllMocks();
    syncState.cb = null;
    (isTauriRuntime as Mock).mockReturnValue(false);
    windowMock.outerPosition.mockResolvedValue({ x: 0, y: 0 });
    windowMock.outerSize.mockResolvedValue({ width: 236, height: 40 });
    windowMock.scaleFactor.mockResolvedValue(1);
    windowMock.setSize.mockResolvedValue(undefined);
    windowMock.setPosition.mockResolvedValue(undefined);
    windowMock.setBackgroundColor.mockResolvedValue(undefined);
    webviewMock.setBackgroundColor.mockResolvedValue(undefined);
  });

  it('renders idle capsule without a track', () => {
    const wrapper = mount(IslandView);
    expect(wrapper.text()).toContain('未播放');
    expect(wrapper.find('.island-capsule').classes()).toContain('is-idle');
    wrapper.unmount();
  });

  it('grants the native window capabilities used by island transitions', () => {
    expect(overlayCapability.permissions).toEqual(expect.arrayContaining([
      'core:window:allow-scale-factor',
      'core:window:allow-set-background-color',
      'core:webview:allow-set-webview-background-color',
    ]));
  });

  it('reasserts transparent native window and webview backgrounds on mount', async () => {
    (isTauriRuntime as Mock).mockReturnValue(true);
    const wrapper = mount(IslandView);
    await flushPromises();

    expect(windowMock.setBackgroundColor).toHaveBeenCalledWith([0, 0, 0, 0]);
    expect(webviewMock.setBackgroundColor).toHaveBeenCalledWith([0, 0, 0, 0]);
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
    // 30/180 → offset = LEN * (1 - 1/6), compact ring radius 15
    const expected = String(2 * Math.PI * 15 * (1 - 30 / 180));
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

  it('offers only the three top docking positions', async () => {
    const wrapper = mount(IslandView);
    await wrapper.get('.island-root').trigger('contextmenu');

    expect(wrapper.findAll('.island-anchor-dot')).toHaveLength(3);
    expect(wrapper.findAll('.island-anchor-dot').map((dot) => dot.attributes('title'))).toEqual([
      'top-left',
      'top-center',
      'top-right',
    ]);
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
    await flushPromises();
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(true);
    expect(wrapper.find('.island-capsule').exists()).toBe(false);

    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    await flushPromises();
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    expect(wrapper.find('.island-capsule').exists()).toBe(true);
    wrapper.unmount();
  });

  it('automatically collapses the expanded card after inactivity', async () => {
    vi.useFakeTimers();
    const wrapper = mount(IslandView);
    await flushPromises();
    const capsule = wrapper.get('.island-capsule');

    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));
    await flushPromises();
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(true);

    await vi.advanceTimersByTimeAsync(6_000);
    await flushPromises();
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    expect(wrapper.find('.island-capsule').exists()).toBe(true);
    wrapper.unmount();
  });

  it('automatically collapses through the real Tauri resize path', async () => {
    vi.useFakeTimers();
    (isTauriRuntime as Mock).mockReturnValue(true);
    const wrapper = mount(IslandView);
    await flushPromises();
    windowMock.setSize.mockClear();
    windowMock.setPosition.mockClear();

    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 20 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 20 }));
    await flushPromises();
    await wrapper.vm.$nextTick();

    expect(windowMock.setSize).toHaveBeenCalledWith(expect.objectContaining({
      width: 360,
      height: 128,
    }));

    await vi.advanceTimersByTimeAsync(5_100);
    await flushPromises();
    await wrapper.vm.$nextTick();

    expect(windowMock.setSize).toHaveBeenLastCalledWith(expect.objectContaining({
      width: 236,
      height: 40,
    }));
    expect(wrapper.find('.island-capsule').exists()).toBe(true);
    await vi.waitFor(() => expect(startVinylSpin).toHaveBeenCalledTimes(3));
    wrapper.unmount();
  });

  it('keeps the card open while the user seeks on the progress slider', async () => {
    const wrapper = mount(IslandView);
    emitState();
    await wrapper.vm.$nextTick();
    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));
    await flushPromises();
    await wrapper.vm.$nextTick();

    const progress = wrapper.get('[role="slider"]');
    progress.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 120, clientY: 20 }));
    progress.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 120, clientY: 20 }));
    await flushPromises();
    await wrapper.vm.$nextTick();

    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(true);
    wrapper.unmount();
  });

  it('does not expand after a drag (pointer travel >= 5px)', async () => {
    const wrapper = mount(IslandView);
    await flushPromises();
    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 130, clientY: 80 }));
    await wrapper.vm.$nextTick();
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    expect(settleCurrentOverlay).toHaveBeenCalledOnce();
    wrapper.unmount();
  });

  it('rebinds the vinyl animation when switching between capsule and card', async () => {
    const wrapper = mount(IslandView);
    await flushPromises();
    await wrapper.vm.$nextTick();
    const spinMock = startVinylSpin as Mock;
    expect(spinMock).toHaveBeenCalledTimes(1);
    const capsuleSpin = spinMock.mock.results[0].value as { kill: Mock };

    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));
    await flushPromises();
    await wrapper.vm.$nextTick();

    expect(capsuleSpin.kill).toHaveBeenCalledOnce();
    expect(spinMock).toHaveBeenCalledTimes(2);
    const cardSpin = spinMock.mock.results[1].value as { kill: Mock };

    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    await flushPromises();
    await wrapper.vm.$nextTick();

    expect(cardSpin.kill).toHaveBeenCalledOnce();
    expect(spinMock).toHaveBeenCalledTimes(3);
    wrapper.unmount();
  });

  it('resizes the window growing downward from the pill (tauri)', async () => {
    (isTauriRuntime as Mock).mockReturnValue(true);
    windowMock.outerPosition.mockResolvedValue({ x: 100, y: 100 });
    windowMock.outerSize.mockResolvedValue({ width: 236, height: 40 });

    const wrapper = mount(IslandView);
    await flushPromises();
    windowMock.setSize.mockClear();
    windowMock.setPosition.mockClear();
    windowMock.outerPosition.mockResolvedValue({ x: 100, y: 0 });
    const capsule = wrapper.get('.island-capsule');
    capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
    capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));

    await vi.waitFor(() => {
      expect(windowMock.setSize).toHaveBeenCalledTimes(1);
    });
    const size = windowMock.setSize.mock.calls[0][0] as { width: number; height: number };
    expect(size).toMatchObject({ width: 360, height: 128 });
    const pos = windowMock.setPosition.mock.calls[0][0] as { x: number; y: number };
    expect(pos).toMatchObject({
      x: Math.round(100 + 118 - 180),
      y: 0,
    });
    wrapper.unmount();
  });

  it('normalizes a stale expanded native window back to the compact size on mount', async () => {
    (isTauriRuntime as Mock).mockReturnValue(true);
    windowMock.outerPosition.mockResolvedValue({ x: 200, y: 16 });
    windowMock.outerSize.mockResolvedValue({ width: 400, height: 152 });

    const wrapper = mount(IslandView);
    await vi.waitFor(() => {
      expect(windowMock.setSize).toHaveBeenCalledWith(expect.objectContaining({
        width: 236,
        height: 40,
      }));
    });
    expect(windowMock.setPosition).toHaveBeenCalledWith(expect.objectContaining({ y: 0 }));
    expect(wrapper.find('.island-capsule').exists()).toBe(true);
    wrapper.unmount();
  });

  it('serializes rapid expand/collapse window resizes', async () => {
    (isTauriRuntime as Mock).mockReturnValue(true);
    let releaseFirstResize!: () => void;
    windowMock.setSize
      .mockImplementationOnce(() => new Promise<void>((resolve) => {
        releaseFirstResize = resolve;
      }))
      .mockResolvedValue(undefined);

    const wrapper = mount(IslandView);
    await flushPromises();
    const capsule = wrapper.get('.island-capsule');
    const clickCapsule = () => {
      capsule.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 100, clientY: 50 }));
      capsule.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: 100, clientY: 50 }));
    };

    clickCapsule();
    await vi.waitFor(() => expect(windowMock.setSize).toHaveBeenCalledTimes(1));
    clickCapsule();
    await Promise.resolve();
    expect(windowMock.setSize).toHaveBeenCalledTimes(1);

    releaseFirstResize();
    await vi.waitFor(() => expect(windowMock.setSize).toHaveBeenCalledTimes(2));
    expect(wrapper.find('[data-test="island-card"]').exists()).toBe(false);
    expect(wrapper.find('.island-capsule').exists()).toBe(true);
    wrapper.unmount();
  });
});

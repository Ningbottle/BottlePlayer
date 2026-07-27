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
  loadLyricPrefs: vi.fn(() => ({ fontSize: 18, density: 'standard', opacity: 100 })),
  saveLyricPrefs: vi.fn((p: unknown) => {
    localStorage.setItem('overlay_lyric_prefs', JSON.stringify(p));
  }),
  saveLyricSize: vi.fn(),
}));

vi.mock('../lyric/useLyricStage', () => ({
  fetchLyrics: vi.fn(async () => [
    { time: 0, text: '第一行' },
    { time: 10, text: '第二行' },
    { time: 20, text: '第三行' },
  ]),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(() => ({
    close: vi.fn(async () => {}),
    onResized: vi.fn(async () => () => {}),
    scaleFactor: vi.fn(async () => 1),
  })),
}));

import DesktopLyricView from '../overlay/DesktopLyricView.vue';

function emitState(partial: Record<string, unknown> = {}) {
  syncState.cb?.({
    hash: 'h1',
    name: '歌词曲',
    artist: '测试歌手',
    cover: '',
    isPlaying: true,
    currentTime: 0,
    duration: 180,
    volume: 0.8,
    loopMode: 'list',
    ...partial,
  });
}

describe('DesktopLyricView', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    syncState.cb = null;
  });

  it('shows idle text without a track', () => {
    const wrapper = mount(DesktopLyricView);
    expect(wrapper.get('[data-test="overlay-lyric-current"]').text()).toContain('未播放');
    wrapper.unmount();
  });

  it('fetches lyrics for the synced track and sweeps the active line', async () => {
    const wrapper = mount(DesktopLyricView);
    emitState({ currentTime: 0 });
    await wrapper.vm.$nextTick();
    // fetchLyrics resolves → first line active
    await wrapper.vm.$nextTick();

    expect(wrapper.get('[data-test="overlay-lyric-current"]').text()).toContain('第一行');

    emitState({ currentTime: 15 });
    await wrapper.vm.$nextTick();
    expect(wrapper.get('[data-test="overlay-lyric-current"]').text()).toContain('第二行');

    // karaoke fill: (15-10)/(20-10) = 50%
    const fill = wrapper.find('.lyric-fill');
    expect(fill.exists()).toBe(true);
    expect(fill.attributes('style')).toContain('width: 50%');
    wrapper.unmount();
  });

  it('shows next line while one is active', async () => {
    const wrapper = mount(DesktopLyricView);
    emitState({ currentTime: 12 });
    await wrapper.vm.$nextTick();
    await wrapper.vm.$nextTick();

    expect(wrapper.find('.lyric-next').text()).toBe('第三行');
    wrapper.unmount();
  });

  it('applies prefs as css vars and toggles the settings panel', async () => {
    localStorage.removeItem('overlay_lyric_prefs');
    const wrapper = mount(DesktopLyricView);
    const bar = wrapper.get('.lyric-bar');
    expect(bar.attributes('style')).toContain('--lyric-font-size: 18px');

    expect(wrapper.find('[data-test="lyric-prefs"]').exists()).toBe(false);
    await wrapper.get('[aria-label="歌词设置"]').trigger('click');
    const panel = wrapper.get('[data-test="lyric-prefs"]');

    const sizeButtons = panel.findAll('.lyric-prefs-row')[0].findAll('button');
    await sizeButtons[3].trigger('click'); // 20
    expect(wrapper.get('.lyric-bar').attributes('style')).toContain('--lyric-font-size: 20px');
    expect(JSON.parse(localStorage.getItem('overlay_lyric_prefs')!)).toMatchObject({ fontSize: 20 });
    wrapper.unmount();
  });
});

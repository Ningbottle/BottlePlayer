import { describe, it, expect, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import AuroraShell from '../AuroraShell.vue';
import NewsprintShell from '../NewsprintShell.vue';

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue(0) }));
vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({ close: vi.fn(), minimize: vi.fn(), toggleMaximize: vi.fn() }),
}));

function shellHeader(shell: typeof AuroraShell, props: Record<string, unknown>) {
  return mount(shell, { props }).find('.app').attributes('data-header');
}

describe('shell data-header attribute', () => {
  it('Aurora: non-playback, non-fullscreen -> merged', () => {
    expect(shellHeader(AuroraShell, { isPlaybackView: false, lyricFullscreen: false })).toBe('merged');
  });

  it('Aurora: playback view -> compact', () => {
    expect(shellHeader(AuroraShell, { isPlaybackView: true, lyricFullscreen: false })).toBe('compact');
  });

  it('Aurora: lyric fullscreen -> compact', () => {
    expect(shellHeader(AuroraShell, { isPlaybackView: false, lyricFullscreen: true })).toBe('compact');
  });

  it('Newsprint: non-playback -> merged', () => {
    expect(shellHeader(NewsprintShell, { isPlaybackView: false, lyricFullscreen: false })).toBe('merged');
  });

  it('Newsprint: playback view -> compact', () => {
    expect(shellHeader(NewsprintShell, { isPlaybackView: true, lyricFullscreen: false })).toBe('compact');
  });
});

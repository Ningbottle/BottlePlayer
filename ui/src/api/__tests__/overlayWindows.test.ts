import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/api/webviewWindow', () => ({
  WebviewWindow: vi.fn(),
}));
vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: vi.fn(),
  currentMonitor: vi.fn(async () => null),
}));
vi.mock('@tauri-apps/api/dpi', () => ({
  LogicalPosition: class {
    constructor(public x: number, public y: number) {}
  },
}));

import { snapToEdges, anchorPosition, loadOverlayPos, saveOverlayPos, resolveCreatePos, loadLyricPrefs, saveLyricPrefs, loadLyricSize, saveLyricSize, SNAP_MARGIN } from '../overlayWindows';

describe('snapToEdges', () => {
  const win = { w: 340, h: 88 };
  const screen = { w: 1920, h: 1080 };

  it('snaps to left and top edges within the margin', () => {
    expect(snapToEdges({ x: 10, y: 20 }, win, screen)).toEqual({ x: 0, y: 0 });
  });

  it('snaps to right and bottom edges within the margin', () => {
    expect(snapToEdges({ x: 1920 - 340 - 12, y: 1080 - 88 - 6 }, win, screen)).toEqual({
      x: 1920 - 340,
      y: 1080 - 88,
    });
  });

  it('leaves positions beyond the margin untouched', () => {
    const free = { x: SNAP_MARGIN + 5, y: 400 };
    expect(snapToEdges(free, win, screen)).toEqual(free);
  });

  it('snaps axes independently', () => {
    expect(snapToEdges({ x: 5, y: 400 }, win, screen)).toEqual({ x: 0, y: 400 });
  });
});

describe('anchorPosition', () => {
  const win = { w: 340, h: 88 };
  const screen = { w: 1920, h: 1080 };

  it('places corners with the margin', () => {
    expect(anchorPosition('top-left', win, screen)).toEqual({ x: 16, y: 16 });
    expect(anchorPosition('bottom-right', win, screen)).toEqual({
      x: 1920 - 340 - 16,
      y: 1080 - 88 - 16,
    });
  });

  it('centers on center-center', () => {
    expect(anchorPosition('center', win, screen)).toEqual({
      x: Math.round((1920 - 340) / 2),
      y: Math.round((1080 - 88) / 2),
    });
  });

  it('mixes edges: top-center and center-left', () => {
    expect(anchorPosition('top-center', win, screen)).toEqual({
      x: Math.round((1920 - 340) / 2),
      y: 16,
    });
    expect(anchorPosition('center-left', win, screen)).toEqual({
      x: 16,
      y: Math.round((1080 - 88) / 2),
    });
  });
});

describe('resolveCreatePos', () => {
  it('places a first-run island at the top-center of the screen', () => {
    const pos = resolveCreatePos({ width: 340, height: 88 }, { w: 1920, h: 1080 });
    expect(pos).toEqual({ x: Math.round((1920 - 340) / 2), y: 16 });
  });

  it('places a first-run lyric bar top-center too', () => {
    const pos = resolveCreatePos({ width: 720, height: 96 }, { w: 1920, h: 1080 });
    expect(pos).toEqual({ x: Math.round((1920 - 720) / 2), y: 16 });
  });
});

describe('lyric prefs persistence', () => {
  beforeEach(() => localStorage.clear());

  it('round-trips prefs and repairs out-of-range values', () => {
    expect(loadLyricPrefs()).toEqual({ fontSize: 18, density: 'standard', opacity: 100 });
    saveLyricPrefs({ fontSize: 24, density: 'compact', opacity: 65 });
    expect(loadLyricPrefs()).toEqual({ fontSize: 24, density: 'compact', opacity: 65 });
    localStorage.setItem('overlay_lyric_prefs', JSON.stringify({ fontSize: 99, density: 'x', opacity: 240 }));
    expect(loadLyricPrefs()).toEqual({ fontSize: 18, density: 'standard', opacity: 100 });
  });

  it('persists lyric width with bounds', () => {
    expect(loadLyricSize()).toBeNull();
    saveLyricSize(860);
    expect(loadLyricSize()).toBe(860);
    localStorage.setItem('overlay_lyric_size', '100');
    expect(loadLyricSize()).toBeNull();
  });
});

describe('overlay position persistence', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('round-trips a saved position', () => {
    saveOverlayPos('island', { x: 120, y: 460 });
    expect(loadOverlayPos('island')).toEqual({ x: 120, y: 460 });
  });

  it('returns null when nothing is stored or data is corrupt', () => {
    expect(loadOverlayPos('lyric')).toBeNull();
    localStorage.setItem('overlay_lyric_pos', '{broken json');
    expect(loadOverlayPos('lyric')).toBeNull();
    localStorage.setItem('overlay_lyric_pos', JSON.stringify({ x: 'a', y: 1 }));
    expect(loadOverlayPos('lyric')).toBeNull();
  });
});

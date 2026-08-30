import { describe, it, expect, vi, beforeEach } from 'vitest';

const {
  currentMonitorMock,
  getByLabelMock,
  getCurrentWindowMock,
  setPositionMock,
  webviewWindowMock,
} = vi.hoisted(() => ({
  currentMonitorMock: vi.fn(),
  getByLabelMock: vi.fn(),
  getCurrentWindowMock: vi.fn(),
  setPositionMock: vi.fn(),
  webviewWindowMock: vi.fn(),
}));

vi.mock('@tauri-apps/api/webviewWindow', () => ({
  WebviewWindow: Object.assign(webviewWindowMock, {
    getByLabel: getByLabelMock,
  }),
}));
vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: getCurrentWindowMock,
  currentMonitor: currentMonitorMock,
}));
vi.mock('@tauri-apps/api/dpi', () => ({
  PhysicalPosition: class {
    constructor(public x: number, public y: number) {}
  },
}));

import {
  SNAP_MARGIN,
  anchorPosition,
  loadLyricPrefs,
  loadLyricSize,
  loadOverlayPos,
  moveCurrentOverlayTo,
  resolveCreatePos,
  saveLyricPrefs,
  saveLyricSize,
  saveOverlayPos,
  settleCurrentOverlay,
  snapToEdges,
  toggleOverlay,
} from '../windows';

describe('overlay toggle result', () => {
  beforeEach(() => {
    getByLabelMock.mockReset();
    getCurrentWindowMock.mockReset();
    currentMonitorMock.mockReset();
    setPositionMock.mockReset();
    webviewWindowMock.mockReset();
    localStorage.clear();
    delete (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__;
  });

  it('reports an unavailable runtime as a failure, not as a closed overlay', async () => {
    await expect(toggleOverlay('island')).resolves.toBe('failed');
  });

  it('reports an existing overlay as closed after closing it', async () => {
    const close = vi.fn().mockResolvedValue(undefined);
    (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__ = {};
    getByLabelMock.mockResolvedValue({ close });

    await expect(toggleOverlay('island')).resolves.toBe('closed');
    expect(close).toHaveBeenCalledOnce();
  });

  it('creates the island with a transparent first-frame background', async () => {
    const setBackgroundColor = vi.fn().mockResolvedValue(undefined);
    const show = vi.fn().mockResolvedValue(undefined);
    const callbacks: { created?: () => void } = {};
    (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__ = {};
    getByLabelMock.mockResolvedValue(null);
    webviewWindowMock.mockImplementation(function MockWebviewWindow() {
      return {
        once: vi.fn(async (event: string, callback: () => void) => {
          if (event === 'tauri://created') callbacks.created = callback;
          return () => {};
        }),
        setBackgroundColor,
        show,
      };
    });

    await expect(toggleOverlay('island')).resolves.toBe('opened');
    expect(webviewWindowMock).toHaveBeenCalledWith('overlay-island', expect.objectContaining({
      width: 236,
      height: 40,
      y: 0,
      visible: false,
      transparent: true,
      backgroundColor: { red: 0, green: 0, blue: 0, alpha: 0 },
    }));
    expect(setBackgroundColor).not.toHaveBeenCalled();
    expect(callbacks.created).toBeTypeOf('function');
    callbacks.created?.();
    await Promise.resolve();
    expect(setBackgroundColor).toHaveBeenCalledWith({ red: 0, green: 0, blue: 0, alpha: 0 });
    expect(show).toHaveBeenCalledOnce();
    expect(setBackgroundColor.mock.invocationCallOrder[0]).toBeLessThan(show.mock.invocationCallOrder[0]);
  });

  it('creates a compact top-docked lyric overlay', async () => {
    (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__ = {};
    getByLabelMock.mockResolvedValue(null);
    webviewWindowMock.mockImplementation(function MockWebviewWindow() {
      return {
        once: vi.fn().mockResolvedValue(() => {}),
        setBackgroundColor: vi.fn().mockResolvedValue(undefined),
        show: vi.fn().mockResolvedValue(undefined),
      };
    });

    await expect(toggleOverlay('lyric')).resolves.toBe('opened');
    expect(webviewWindowMock).toHaveBeenCalledWith('overlay-lyric', expect.objectContaining({
      width: 560,
      height: 80,
      minWidth: 420,
      maxWidth: 640,
      y: 0,
    }));
  });
});

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
    expect(anchorPosition('top-left', win, screen)).toEqual({ x: 16, y: 0 });
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
      y: 0,
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
    expect(pos).toEqual({ x: Math.round((1920 - 340) / 2), y: 0 });
  });

  it('places a first-run lyric bar top-center too', () => {
    const pos = resolveCreatePos({ width: 720, height: 96 }, { w: 1920, h: 1080 });
    expect(pos).toEqual({ x: Math.round((1920 - 720) / 2), y: 0 });
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
    saveLyricSize(600);
    expect(loadLyricSize()).toBe(600);
    localStorage.setItem('overlay_lyric_size', '860');
    expect(loadLyricSize()).toBeNull();
    localStorage.setItem('overlay_lyric_size', '100');
    expect(loadLyricSize()).toBeNull();
  });
});

describe('top docking', () => {
  beforeEach(() => {
    localStorage.clear();
    setPositionMock.mockReset().mockResolvedValue(undefined);
    getCurrentWindowMock.mockReset().mockReturnValue({
      outerPosition: vi.fn(async () => ({ x: 120, y: 420 })),
      outerSize: vi.fn(async () => ({ width: 560, height: 80 })),
      setPosition: setPositionMock,
    });
    currentMonitorMock.mockReset().mockResolvedValue({
      size: { width: 1920, height: 1080 },
    });
    (window as Window & { __TAURI_INTERNALS__?: unknown }).__TAURI_INTERNALS__ = {};
  });

  it('returns both overlay kinds to the fixed top inset after dragging', async () => {
    await settleCurrentOverlay('lyric');

    expect(setPositionMock).toHaveBeenCalledWith(expect.objectContaining({ x: 120, y: 0 }));
    expect(loadOverlayPos('lyric')).toEqual({ x: 120, y: 0 });
  });

  it('keeps a lyric anchor on the top edge even if a lower anchor is requested', async () => {
    await moveCurrentOverlayTo('bottom-right', 'lyric');

    expect(setPositionMock).toHaveBeenCalledWith(expect.objectContaining({
      x: 1920 - 560 - 16,
      y: 0,
    }));
    expect(loadOverlayPos('lyric')).toEqual({ x: 1920 - 560 - 16, y: 0 });
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

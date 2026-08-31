import { describe, it, expect, vi, beforeEach } from 'vitest';

const {
  getCurrentWindowMock,
  getCurrentWebviewMock,
} = vi.hoisted(() => ({
  getCurrentWindowMock: vi.fn(),
  getCurrentWebviewMock: vi.fn(),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: getCurrentWindowMock,
  currentMonitor: vi.fn(),
}));
vi.mock('@tauri-apps/api/webview', () => ({
  getCurrentWebview: getCurrentWebviewMock,
}));
vi.mock('@tauri-apps/api/dpi', () => ({
  PhysicalPosition: class {
    constructor(public x: number, public y: number) {}
  },
  LogicalSize: class {
    constructor(public width: number, public height: number) {}
  },
}));
vi.mock('@tauri-apps/api/webviewWindow', () => ({
  WebviewWindow: vi.fn(),
}));

import {
  minimizeCurrentWindow,
  toggleMaximizeCurrentWindow,
  closeCurrentWindow,
  readCurrentWindowFrame,
  setCurrentWindowLogicalSize,
  setCurrentWindowPhysicalPosition,
  makeCurrentOverlayTransparent,
  onCurrentWindowResized,
  getCurrentWindowScaleFactor,
  type WindowFrame,
} from '../windows';

function fakeWindow(overrides: Record<string, unknown> = {}) {
  return {
    outerPosition: vi.fn().mockResolvedValue({ x: 100, y: 40 }),
    outerSize: vi.fn().mockResolvedValue({ width: 320, height: 90 }),
    scaleFactor: vi.fn().mockResolvedValue(2),
    setSize: vi.fn().mockResolvedValue(undefined),
    setPosition: vi.fn().mockResolvedValue(undefined),
    minimize: vi.fn().mockResolvedValue(undefined),
    toggleMaximize: vi.fn().mockResolvedValue(undefined),
    close: vi.fn().mockResolvedValue(undefined),
    setBackgroundColor: vi.fn().mockResolvedValue(undefined),
    onResized: vi.fn().mockResolvedValue(vi.fn()),
    ...overrides,
  };
}

describe('platform/tauri window action adapters', () => {
  beforeEach(() => {
    getCurrentWindowMock.mockReset();
    getCurrentWebviewMock.mockReset();
  });

  it('minimize/toggleMaximize/close delegate to the current window', async () => {
    const win = fakeWindow();
    getCurrentWindowMock.mockReturnValue(win);

    await minimizeCurrentWindow();
    await toggleMaximizeCurrentWindow();
    await closeCurrentWindow();

    expect(win.minimize).toHaveBeenCalledTimes(1);
    expect(win.toggleMaximize).toHaveBeenCalledTimes(1);
    expect(win.close).toHaveBeenCalledTimes(1);
  });

  it('readCurrentWindowFrame returns a neutral WindowFrame in physical px', async () => {
    const win = fakeWindow();
    getCurrentWindowMock.mockReturnValue(win);

    const frame: WindowFrame = await readCurrentWindowFrame();

    expect(frame).toEqual({
      position: { x: 100, y: 40 },
      size: { width: 320, height: 90 },
      scaleFactor: 2,
    });
  });

  it('setCurrentWindowLogicalSize sets logical size (no raw LogicalSize leaks)', async () => {
    const win = fakeWindow();
    getCurrentWindowMock.mockReturnValue(win);

    await setCurrentWindowLogicalSize(480, 120);

    expect(win.setSize).toHaveBeenCalledTimes(1);
    const [arg] = win.setSize.mock.calls[0];
    expect(arg).toEqual({ width: 480, height: 120 });
  });

  it('setCurrentWindowPhysicalPosition sets a physical position', async () => {
    const win = fakeWindow();
    getCurrentWindowMock.mockReturnValue(win);

    await setCurrentWindowPhysicalPosition(12, 34);

    expect(win.setPosition).toHaveBeenCalledTimes(1);
    expect(win.setPosition.mock.calls[0][0]).toEqual({ x: 12, y: 34 });
  });

  it('makeCurrentOverlayTransparent clears window + webview backgrounds', async () => {
    const win = fakeWindow();
    const webview = { setBackgroundColor: vi.fn().mockResolvedValue(undefined) };
    getCurrentWindowMock.mockReturnValue(win);
    getCurrentWebviewMock.mockReturnValue(webview);

    await makeCurrentOverlayTransparent();

    expect(win.setBackgroundColor).toHaveBeenCalledWith([0, 0, 0, 0]);
    expect(webview.setBackgroundColor).toHaveBeenCalledWith([0, 0, 0, 0]);
  });

  it('onCurrentWindowResized yields logical width + scaleFactor via neutral payload', async () => {
    const unlisten = vi.fn();
    const win = fakeWindow({
      onResized: vi.fn().mockResolvedValue(unlisten),
    });
    getCurrentWindowMock.mockReturnValue(win);

    let captured: ((event: { payload: { width: number; height: number } }) => void) | null = null;
    win.onResized.mockImplementation(async (cb: never) => {
      captured = cb;
      return unlisten;
    });

    const returned = await onCurrentWindowResized((event) => {
      void event;
    });

    expect(typeof returned).toBe('function');
    expect(captured).not.toBeNull();
    captured!({ payload: { width: 640, height: 180 } });
  });

  it('getCurrentWindowScaleFactor reads only scaleFactor — no outerPosition/outerSize IPC', async () => {
    const win = {
      scaleFactor: vi.fn().mockResolvedValue(1.75),
      outerPosition: vi.fn().mockResolvedValue({ x: 0, y: 0 }),
      outerSize: vi.fn().mockResolvedValue({ width: 800, height: 600 }),
    };
    getCurrentWindowMock.mockReturnValue(win);

    const factor = await getCurrentWindowScaleFactor();

    expect(factor).toBe(1.75);
    expect(win.scaleFactor).toHaveBeenCalledTimes(1);
    expect(win.outerPosition).not.toHaveBeenCalled();
    expect(win.outerSize).not.toHaveBeenCalled();
  });
});

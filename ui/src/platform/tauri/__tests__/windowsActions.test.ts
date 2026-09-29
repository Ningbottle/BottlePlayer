import { describe, it, expect, vi, beforeEach } from 'vitest';

const { getCurrentWindowMock } = vi.hoisted(() => ({
  getCurrentWindowMock: vi.fn(),
}));

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: getCurrentWindowMock,
}));

import {
  minimizeCurrentWindow,
  toggleMaximizeCurrentWindow,
  closeCurrentWindow,
} from '../windows';

function fakeWindow(overrides: Record<string, unknown> = {}) {
  return {
    minimize: vi.fn().mockResolvedValue(undefined),
    toggleMaximize: vi.fn().mockResolvedValue(undefined),
    close: vi.fn().mockResolvedValue(undefined),
    ...overrides,
  };
}

describe('platform/tauri window action adapters', () => {
  beforeEach(() => {
    getCurrentWindowMock.mockReset();
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
});

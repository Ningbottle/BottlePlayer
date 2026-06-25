import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(false),
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { initPlayerBackend, playerStore } from '../../api/playerStore';

describe('initPlayerBackend HTML5 fallback', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.audio = document.createElement('audio');
    playerStore.backend = null;
  });

  it('falls back to HTML5 when both MFS and MFP fail to initialize', async () => {
    await initPlayerBackend();

    expect(playerStore.backend).toBe('html5');
    expect(playerStore.audio).toBeTruthy();
  });
});

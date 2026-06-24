import { describe, it, expect, vi, beforeEach } from 'vitest';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(false),
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { initPlayerBackend, playerStore } from '../../api/playerStore';
import { invoke } from '@tauri-apps/api/core';

describe('initPlayerBackend HTML5 fallback', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.audio = document.createElement('audio');
    playerStore.backend = null;
  });

  it('falls back to HTML5 when both MFS and MFP fail to initialize', async () => {
    (invoke as any).mockResolvedValue(false);

    await initPlayerBackend();

    expect(playerStore.backend).toBe('html5');
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 1 });
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 0 });
  });
});

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

describe('recent history module initialization without storage', () => {
  beforeEach(() => { vi.resetModules(); localStorage.clear(); });
  afterEach(() => { vi.restoreAllMocks(); });

  it.each(['getter', 'getItem', 'setItem'] as const)('keeps in-memory playback history when %s throws', async (failure) => {
    const fail = () => { throw new DOMException('blocked', 'SecurityError'); };
    if (failure === 'getter') vi.spyOn(window, 'localStorage', 'get').mockImplementation(fail);
    else vi.spyOn(Storage.prototype, failure).mockImplementation(fail);
    const { recentPlayedStore } = await import('../recentPlayedStore');
    expect(() => recentPlayedStore.recordRecentPlayed({
      FileHash: 'offline-track', SongName: '本地试听', SingerName: '测试', Duration: 120,
    })).not.toThrow();
    expect(recentPlayedStore.entries.value[0].FileHash).toBe('offline-track');
    expect(() => recentPlayedStore.reset()).not.toThrow();
    expect(recentPlayedStore.entries.value).toEqual([]);
  });
});

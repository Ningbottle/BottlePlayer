import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { PLAYER_VOLUME_KEY, loadPlayerVolume, savePlayerVolume } from '../playerPersistence';

describe('playerPersistence: player_volume single owner', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    vi.restoreAllMocks();
    localStorage.removeItem(PLAYER_VOLUME_KEY);
  });

  it('uses the stable player_volume key', () => {
    expect(PLAYER_VOLUME_KEY).toBe('player_volume');
  });

  it('loadPlayerVolume returns a stored valid value as-is', () => {
    localStorage.setItem(PLAYER_VOLUME_KEY, '0.35');
    expect(loadPlayerVolume()).toBe(0.35);
  });

  it.each([
    ['not-a-number', 0.7],
    ['', 0.7],
    [null, 0.7],
  ] as const)('loadPlayerVolume falls back to 0.7 for invalid value %j', (raw, expected) => {
    if (raw !== null) localStorage.setItem(PLAYER_VOLUME_KEY, raw);
    expect(loadPlayerVolume()).toBe(expected);
  });

  it('loadPlayerVolume clamps values above 1 to 1', () => {
    localStorage.setItem(PLAYER_VOLUME_KEY, '5');
    expect(loadPlayerVolume()).toBe(1);
  });

  it('loadPlayerVolume clamps negative values to 0', () => {
    localStorage.setItem(PLAYER_VOLUME_KEY, '-1');
    expect(loadPlayerVolume()).toBe(0);
  });

  it('loadPlayerVolume returns 0.7 without throwing when localStorage.getItem throws', () => {
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
      throw new Error('SecurityError: Access denied');
    });

    expect(() => loadPlayerVolume()).not.toThrow();
    expect(loadPlayerVolume()).toBe(0.7);
  });

  it('savePlayerVolume writes the key with String(volume)', () => {
    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');

    savePlayerVolume(0.42);

    expect(setItemSpy).toHaveBeenCalledWith(PLAYER_VOLUME_KEY, '0.42');
    expect(localStorage.getItem(PLAYER_VOLUME_KEY)).toBe('0.42');
  });

  it('savePlayerVolume swallows a setItem failure without throwing', () => {
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('QuotaExceededError');
    });

    expect(() => savePlayerVolume(0.42)).not.toThrow();
    expect(localStorage.getItem(PLAYER_VOLUME_KEY)).toBeNull();
  });
});

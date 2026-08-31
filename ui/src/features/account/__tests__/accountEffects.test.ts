import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  configureAccountEffects,
  notifyAccountReady,
  notifyAccountCleared,
  notifyLocalLogout,
  __resetAccountEffectsForTests,
  type AccountEffects,
} from '../accountEffects';

describe('accountEffects port', () => {
  beforeEach(() => {
    __resetAccountEffectsForTests();
  });

  it('does not throw when no effects are configured', () => {
    expect(() => notifyAccountReady('u1')).not.toThrow();
    expect(() => notifyAccountCleared()).not.toThrow();
    expect(() => notifyLocalLogout()).not.toThrow();
  });

  it('forwards each notification exactly once after configure', () => {
    const effects: AccountEffects = {
      onAccountReady: vi.fn(),
      onAccountCleared: vi.fn(),
      onLocalLogout: vi.fn(),
    };
    configureAccountEffects(effects);

    notifyAccountReady('u1');
    notifyAccountCleared();
    notifyLocalLogout();

    expect(effects.onAccountReady).toHaveBeenCalledTimes(1);
    expect(effects.onAccountCleared).toHaveBeenCalledTimes(1);
    expect(effects.onLocalLogout).toHaveBeenCalledTimes(1);
  });

  it('forwards userId unchanged', () => {
    const onAccountReady = vi.fn();
    configureAccountEffects({ onAccountReady, onAccountCleared: vi.fn(), onLocalLogout: vi.fn() });

    notifyAccountReady('user-abc-123');

    expect(onAccountReady).toHaveBeenCalledWith('user-abc-123');
  });

  it('stops calling previous effects after reset', () => {
    const effects: AccountEffects = {
      onAccountReady: vi.fn(),
      onAccountCleared: vi.fn(),
      onLocalLogout: vi.fn(),
    };
    configureAccountEffects(effects);
    __resetAccountEffectsForTests();

    notifyAccountReady('u1');
    notifyAccountCleared();
    notifyLocalLogout();

    expect(effects.onAccountReady).not.toHaveBeenCalled();
    expect(effects.onAccountCleared).not.toHaveBeenCalled();
    expect(effects.onLocalLogout).not.toHaveBeenCalled();
  });

  it('returns the Promise from a Promise-returning onAccountReady unchanged', async () => {
    const sentinel = Promise.resolve();
    const onAccountReady = vi.fn(() => sentinel);
    configureAccountEffects({ onAccountReady, onAccountCleared: vi.fn(), onLocalLogout: vi.fn() });

    const returned = notifyAccountReady('u1');

    expect(returned).toBe(sentinel);
    await returned;
    expect(onAccountReady).toHaveBeenCalledTimes(1);
  });

  it('does not swallow synchronous throws from configured effects', () => {
    const boom = new Error('sync failure');
    configureAccountEffects({
      onAccountReady: () => { throw boom; },
      onAccountCleared: vi.fn(),
      onLocalLogout: vi.fn(),
    });

    expect(() => notifyAccountReady('u1')).toThrow(boom);
  });
});

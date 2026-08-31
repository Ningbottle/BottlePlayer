import { describe, it, expect, beforeEach } from 'vitest';
import {
  nextHomeEnterMode,
  __resetHomeEnterSessionForTest,
} from '../homeEnterSession';

describe('homeEnterSession', () => {
  beforeEach(() => {
    __resetHomeEnterSessionForTest();
  });

  it('returns cold on the first activation', () => {
    expect(nextHomeEnterMode()).toBe('cold');
  });

  it('returns return on later activations', () => {
    expect(nextHomeEnterMode()).toBe('cold');
    expect(nextHomeEnterMode()).toBe('return');
    expect(nextHomeEnterMode()).toBe('return');
  });

  it('reset returns the session to cold', () => {
    nextHomeEnterMode();
    nextHomeEnterMode();
    __resetHomeEnterSessionForTest();
    expect(nextHomeEnterMode()).toBe('cold');
  });
});

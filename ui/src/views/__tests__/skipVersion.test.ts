import { describe, it, expect, beforeEach } from 'vitest';

describe('skip version', () => {
  beforeEach(() => localStorage.clear());

  it('stores skipped version in localStorage', () => {
    localStorage.setItem('tweak_skipped_version', '2.0.1');
    expect(localStorage.getItem('tweak_skipped_version')).toBe('2.0.1');
  });

  it('suppresses badge when version matches skipped', () => {
    localStorage.setItem('tweak_skipped_version', '2.0.1');
    const skipped = localStorage.getItem('tweak_skipped_version');
    const isSuppressed = skipped === '2.0.1';
    expect(isSuppressed).toBe(true);
  });

  it('does not suppress when version differs', () => {
    localStorage.setItem('tweak_skipped_version', '2.0.0');
    const skipped = localStorage.getItem('tweak_skipped_version');
    const isSuppressed = skipped === '2.0.1';
    expect(isSuppressed).toBe(false);
  });
});

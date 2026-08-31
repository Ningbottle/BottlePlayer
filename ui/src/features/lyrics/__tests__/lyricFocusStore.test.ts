import { describe, it, expect, beforeEach } from 'vitest';
import { useLyricFocusStore, __resetLyricFocusForTest } from '../lyricFocusStore';

describe('lyricFocusStore', () => {
  beforeEach(() => {
    localStorage.clear();
    __resetLyricFocusForTest();
  });

  it('defaults to readable after init', () => {
    const store = useLyricFocusStore();
    store.init();
    expect(store.mode.value).toBe('readable'); // RED on stub: receives 'stage'
  });

  it('persists stage and reloads it', () => {
    const store = useLyricFocusStore();
    store.init();
    store.setMode('stage');
    expect(localStorage.getItem('tweak_lyric_focus')).toBe('stage');
    __resetLyricFocusForTest();
    const again = useLyricFocusStore();
    again.init();
    expect(again.mode.value).toBe('stage'); // RED if init ignores storage
  });

  it('toggle flips readable ↔ stage', () => {
    const store = useLyricFocusStore();
    store.init();
    // after GREEN, init is readable
    store.setMode('readable');
    store.toggle();
    expect(store.mode.value).toBe('stage');
    store.toggle();
    expect(store.mode.value).toBe('readable');
  });

  it('invalid storage falls back to readable', () => {
    localStorage.setItem('tweak_lyric_focus', 'nope');
    const store = useLyricFocusStore();
    store.init();
    expect(store.mode.value).toBe('readable');
  });
});

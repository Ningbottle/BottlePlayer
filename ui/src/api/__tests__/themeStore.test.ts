import { describe, it, expect, beforeEach } from 'vitest';
import { useThemeStore, __resetForTest } from '../themeStore';

describe('themeStore', () => {
  beforeEach(() => {
    localStorage.clear();
    __resetForTest();
    document.documentElement.removeAttribute('data-skin');
    document.documentElement.removeAttribute('data-mode');
  });

  it('setSkin writes data-skin attribute and persists to localStorage', () => {
    const store = useThemeStore();
    store.setSkin('newsprint');
    expect(document.documentElement.dataset.skin).toBe('newsprint');
    expect(localStorage.getItem('tweak_skin')).toBe('newsprint');
    expect(store.skinId.value).toBe('newsprint');
  });

  it('setMode writes data-mode attribute and persists to localStorage', () => {
    const store = useThemeStore();
    store.setMode('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
    expect(localStorage.getItem('tweak_mode')).toBe('dark');
    expect(store.mode.value).toBe('dark');
  });

  it('init reads stored skin+mode and applies to DOM', () => {
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    const store = useThemeStore();
    store.init();
    expect(document.documentElement.dataset.skin).toBe('newsprint');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });

  it('init defaults to aurora+light when nothing stored', () => {
    const store = useThemeStore();
    store.init();
    expect(document.documentElement.dataset.skin).toBe('aurora');
    expect(document.documentElement.dataset.mode).toBe('light');
  });
});

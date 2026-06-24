import { describe, it, expect, beforeEach } from 'vitest';
import { useThemeStore, __resetForTest } from '../themeStore';

describe('themeStore', () => {
  beforeEach(() => {
    localStorage.clear();
    document.documentElement.removeAttribute('data-skin');
    document.documentElement.removeAttribute('data-mode');
    __resetForTest();
  });

  it('setSkin sets data-skin attribute on html', () => {
    const store = useThemeStore();
    store.setSkin('aurora');
    expect(document.documentElement.dataset.skin).toBe('aurora');
  });

  it('setSkin persists to localStorage', () => {
    const store = useThemeStore();
    store.setSkin('aurora');
    expect(localStorage.getItem('tweak_skin')).toBe('aurora');
  });

  it('setMode sets data-mode attribute', () => {
    const store = useThemeStore();
    store.setMode('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });

  it('init reads from localStorage', () => {
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    const store = useThemeStore();
    store.init();
    expect(store.skinId.value).toBe('newsprint');
    expect(store.mode.value).toBe('dark');
  });

  it('defaults to aurora/light when nothing stored', () => {
    const store = useThemeStore();
    store.init();
    expect(store.skinId.value).toBe('aurora');
    expect(store.mode.value).toBe('light');
  });
});

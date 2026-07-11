import { describe, it, expect, beforeEach } from 'vitest';
import { useThemeStore, __resetForTest, type SkinId, type Mode } from '../themeStore';

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

  it('all four skin+mode combinations apply both data attributes', () => {
    const store = useThemeStore();
    const combos: [SkinId, Mode][] = [
      ['aurora', 'light'],
      ['aurora', 'dark'],
      ['newsprint', 'light'],
      ['newsprint', 'dark'],
    ];
    for (const [skin, mode] of combos) {
      store.setSkin(skin);
      store.setMode(mode);
      expect(document.documentElement.dataset.skin).toBe(skin);
      expect(document.documentElement.dataset.mode).toBe(mode);
    }
  });
});

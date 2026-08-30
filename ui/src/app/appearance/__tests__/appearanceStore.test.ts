import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
  __resetForTest as resetAppearance,
  useAppearanceStore,
  type AppearanceSettings,
} from '../appearanceStore';
import { __resetForTest as resetTheme, useThemeStore } from '../themeStore';

const defaults: AppearanceSettings = {
  skin: 'aurora',
  mode: 'light',
  accent: '#18875b',
  compactList: false,
  lyricAlign: 'left',
};

function root() {
  return document.documentElement;
}

describe('appearanceStore', () => {
  beforeEach(() => {
    localStorage.clear();
    resetAppearance();
    resetTheme();
    root().removeAttribute('data-skin');
    root().removeAttribute('data-mode');
    root().removeAttribute('data-compact-list');
    root().removeAttribute('data-lyric-align');
    root().classList.remove('compact', 'lyric-left');
    root().style.removeProperty('--accent');
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('falls back to defaults for invalid canonical values', () => {
    localStorage.setItem('appearance_skin', 'dark');
    localStorage.setItem('appearance_mode', 'purple');
    localStorage.setItem('appearance_accent', '');
    localStorage.setItem('appearance_compact_list', 'maybe');
    localStorage.setItem('appearance_lyric_align', 'right');
    const getItem = vi.spyOn(Storage.prototype, 'getItem');

    const store = useAppearanceStore();
    store.init();

    expect(getItem).toHaveBeenCalledTimes(5);
    expect({
      skin: store.skin.value,
      mode: store.mode.value,
      accent: store.accent.value,
      compactList: store.compactList.value,
      lyricAlign: store.lyricAlign.value,
    }).toEqual(defaults);
    expect(root().style.getPropertyValue('--accent')).toBe('');
    expect(root().classList.contains('compact')).toBe(false);
    expect(root().classList.contains('lyric-left')).toBe(true);
  });

  it('loads valid canonical values into the reactive settings and DOM', () => {
    localStorage.setItem('appearance_skin', 'newsprint');
    localStorage.setItem('appearance_mode', 'dark');
    localStorage.setItem('appearance_accent', '#ff0000');
    localStorage.setItem('appearance_compact_list', 'true');
    localStorage.setItem('appearance_lyric_align', 'center');

    const store = useAppearanceStore();
    store.init();

    expect(store.skin.value).toBe('newsprint');
    expect(store.mode.value).toBe('dark');
    expect(store.accent.value).toBe('#ff0000');
    expect(store.compactList.value).toBe(true);
    expect(store.lyricAlign.value).toBe('center');
    expect(root().style.getPropertyValue('--accent')).toBe('#ff0000');
    expect(root().classList.contains('compact')).toBe(true);
    expect(root().classList.contains('lyric-left')).toBe(false);
  });

  it('does not reread storage or reapply any DOM field after the first init', () => {
    localStorage.setItem('appearance_skin', 'aurora');
    localStorage.setItem('appearance_mode', 'light');
    localStorage.setItem('appearance_compact_list', 'false');
    localStorage.setItem('appearance_lyric_align', 'left');
    const getItem = vi.spyOn(Storage.prototype, 'getItem');
    const store = useAppearanceStore();
    store.init();
    const firstReadCount = getItem.mock.calls.length;

    expect(root().dataset.skin).toBe('aurora');
    expect(root().dataset.mode).toBe('light');
    expect(root().dataset.compactList).toBe('false');
    expect(root().dataset.lyricAlign).toBe('left');
    expect(root().classList.contains('compact')).toBe(false);
    expect(root().classList.contains('lyric-left')).toBe(true);
    expect(root().style.getPropertyValue('--accent')).toBe('');
    expect(store.accent.value).toBe('#18875b');

    root().dataset.skin = 'manual';
    root().dataset.mode = 'manual';
    root().dataset.compactList = 'manual';
    root().dataset.lyricAlign = 'manual';
    root().classList.remove('lyric-left');
    root().classList.add('compact');
    root().style.setProperty('--accent', '#manual');
    store.init();

    expect(getItem).toHaveBeenCalledTimes(firstReadCount);
    expect(root().dataset.skin).toBe('manual');
    expect(root().dataset.mode).toBe('manual');
    expect(root().dataset.compactList).toBe('manual');
    expect(root().dataset.lyricAlign).toBe('manual');
    expect(root().classList.contains('compact')).toBe(true);
    expect(root().classList.contains('lyric-left')).toBe(false);
    expect(root().style.getPropertyValue('--accent')).toBe('#manual');
  });

  it('persists each setting under its canonical key', () => {
    const store = useAppearanceStore();
    store.setSkin('newsprint');
    store.setMode('dark');
    store.setAccent('#ff0000');
    store.setCompactList(true);
    store.setLyricAlign('center');

    expect(localStorage.getItem('appearance_skin')).toBe('newsprint');
    expect(localStorage.getItem('appearance_mode')).toBe('dark');
    expect(localStorage.getItem('appearance_accent')).toBe('#ff0000');
    expect(localStorage.getItem('appearance_compact_list')).toBe('true');
    expect(localStorage.getItem('appearance_lyric_align')).toBe('center');
  });

  it('uses skin and mode token accents without creating inline defaults', () => {
    const store = useAppearanceStore();
    store.init();
    expect(store.accent.value).toBe('#18875b');
    expect(root().style.getPropertyValue('--accent')).toBe('');

    store.setSkin('newsprint');
    expect(store.accent.value).toBe('#a8311b');
    expect(root().style.getPropertyValue('--accent')).toBe('');

    store.setMode('dark');
    expect(store.accent.value).toBe('#c4391e');
    expect(root().style.getPropertyValue('--accent')).toBe('');

    store.setSkin('aurora');
    expect(store.accent.value).toBe('#62d6a2');
    expect(root().style.getPropertyValue('--accent')).toBe('');

    store.setMode('light');
    expect(store.accent.value).toBe('#18875b');
    expect(root().style.getPropertyValue('--accent')).toBe('');
  });

  it('keeps a custom accent inline while skin and mode change', () => {
    const store = useAppearanceStore();
    store.setAccent('#abcdef');
    store.setSkin('newsprint');
    store.setMode('dark');

    expect(store.accent.value).toBe('#abcdef');
    expect(root().style.getPropertyValue('--accent')).toBe('#abcdef');
  });

  it('toggles legacy compact and lyric alignment classes with their datasets', () => {
    const store = useAppearanceStore();
    store.setCompactList(true);
    expect(root().dataset.compactList).toBe('true');
    expect(root().classList.contains('compact')).toBe(true);
    store.setCompactList(false);
    expect(root().dataset.compactList).toBe('false');
    expect(root().classList.contains('compact')).toBe(false);

    store.setLyricAlign('center');
    expect(root().dataset.lyricAlign).toBe('center');
    expect(root().classList.contains('lyric-left')).toBe(false);
    store.setLyricAlign('left');
    expect(root().dataset.lyricAlign).toBe('left');
    expect(root().classList.contains('lyric-left')).toBe(true);
  });

  it('loads valid legacy Drawer keys only when canonical keys are absent', () => {
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    localStorage.setItem('tweak_accent', '#1234');
    localStorage.setItem('tweak_compact', 'true');
    localStorage.setItem('tweak_lyric_align', 'center');
    const store = useAppearanceStore();

    store.init();

    expect(store.skin.value).toBe('newsprint');
    expect(store.mode.value).toBe('dark');
    expect(store.accent.value).toBe('#1234');
    expect(store.compactList.value).toBe(true);
    expect(store.lyricAlign.value).toBe('center');
    expect(root().style.getPropertyValue('--accent')).toBe('#1234');
    expect(root().classList.contains('compact')).toBe(true);
    expect(root().classList.contains('lyric-left')).toBe(false);
  });

  it('falls back for invalid legacy Drawer keys without using them as custom state', () => {
    localStorage.setItem('tweak_skin', 'purple');
    localStorage.setItem('tweak_mode', 'sepia');
    localStorage.setItem('tweak_accent', '#12345');
    localStorage.setItem('tweak_compact', 'maybe');
    localStorage.setItem('tweak_lyric_align', 'right');
    const store = useAppearanceStore();

    store.init();

    expect({
      skin: store.skin.value,
      mode: store.mode.value,
      accent: store.accent.value,
      compactList: store.compactList.value,
      lyricAlign: store.lyricAlign.value,
    }).toEqual(defaults);
    expect(root().style.getPropertyValue('--accent')).toBe('');
    expect(root().classList.contains('compact')).toBe(false);
    expect(root().classList.contains('lyric-left')).toBe(true);
  });

  it('gives canonical values precedence over valid legacy values', () => {
    localStorage.setItem('appearance_skin', 'aurora');
    localStorage.setItem('appearance_mode', 'light');
    localStorage.setItem('appearance_accent', '#123456');
    localStorage.setItem('appearance_compact_list', 'false');
    localStorage.setItem('appearance_lyric_align', 'left');
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    localStorage.setItem('tweak_accent', '#abcdef');
    localStorage.setItem('tweak_compact', 'true');
    localStorage.setItem('tweak_lyric_align', 'center');
    const store = useAppearanceStore();

    store.init();

    expect(store.skin.value).toBe('aurora');
    expect(store.mode.value).toBe('light');
    expect(store.accent.value).toBe('#123456');
    expect(store.compactList.value).toBe(false);
    expect(store.lyricAlign.value).toBe('left');
  });

  it('does not let invalid canonical values fall through to valid legacy values', () => {
    localStorage.setItem('appearance_skin', 'invalid');
    localStorage.setItem('appearance_mode', 'invalid');
    localStorage.setItem('appearance_accent', '--foo');
    localStorage.setItem('appearance_compact_list', 'maybe');
    localStorage.setItem('appearance_lyric_align', 'right');
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    localStorage.setItem('tweak_accent', '#abcdef');
    localStorage.setItem('tweak_compact', 'true');
    localStorage.setItem('tweak_lyric_align', 'center');
    const store = useAppearanceStore();

    store.init();

    expect(store.skin.value).toBe('aurora');
    expect(store.mode.value).toBe('light');
    expect(store.accent.value).toBe('#18875b');
    expect(store.compactList.value).toBe(false);
    expect(store.lyricAlign.value).toBe('left');
    expect(root().style.getPropertyValue('--accent')).toBe('');
  });

  it.each(['#12345', '#1234567', '--foo'])('rejects invalid accent %s', (value) => {
    localStorage.setItem('appearance_accent', value);
    const store = useAppearanceStore();

    store.init();

    expect(store.accent.value).toBe('#18875b');
    expect(root().style.getPropertyValue('--accent')).toBe('');
  });

  it.each(['#123', '#1234', '#123456', '#12345678'])('accepts valid accent %s', (value) => {
    localStorage.setItem('appearance_accent', value);
    const store = useAppearanceStore();

    store.init();

    expect(store.accent.value).toBe(value);
    expect(root().style.getPropertyValue('--accent')).toBe(value);
  });

  it('resets refs and every appearance DOM side effect deterministically', () => {
    const store = useAppearanceStore();
    store.setSkin('newsprint');
    store.setMode('dark');
    store.setAccent('#abcdef');
    store.setCompactList(true);
    store.setLyricAlign('center');

    resetAppearance();

    expect(store.skin.value).toBe('aurora');
    expect(store.mode.value).toBe('light');
    expect(store.accent.value).toBe('#18875b');
    expect(store.compactList.value).toBe(false);
    expect(store.lyricAlign.value).toBe('left');
    expect(root().getAttribute('data-skin')).toBe(null);
    expect(root().getAttribute('data-mode')).toBe(null);
    expect(root().getAttribute('data-compact-list')).toBe(null);
    expect(root().getAttribute('data-lyric-align')).toBe(null);
    expect(root().classList.contains('compact')).toBe(false);
    expect(root().classList.contains('lyric-left')).toBe(false);
    expect(root().style.getPropertyValue('--accent')).toBe('');
  });

  it('keeps themeStore refs and setters backed by appearanceStore', () => {
    localStorage.setItem('appearance_skin', 'newsprint');
    localStorage.setItem('appearance_mode', 'dark');
    const appearance = useAppearanceStore();
    const theme = useThemeStore();

    theme.init();

    expect(appearance.skin.value).toBe('newsprint');
    expect(appearance.mode.value).toBe('dark');
    expect(theme.skinId.value).toBe('newsprint');
    expect(theme.mode.value).toBe('dark');
    theme.setSkin('aurora');
    expect(appearance.skin.value).toBe('aurora');
    expect(root().dataset.skin).toBe('aurora');
  });

  it('does not expose excluded appearance controls', () => {
    const store = useAppearanceStore();
    expect(store).not.toHaveProperty('font');
    expect(store).not.toHaveProperty('fontFamily');
    expect(store).not.toHaveProperty('fontSize');
    expect(store).not.toHaveProperty('background');
    expect(store).not.toHaveProperty('warmth');
    expect(store).not.toHaveProperty('blur');
    expect(store).not.toHaveProperty('grain');
  });
});

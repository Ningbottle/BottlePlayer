import { ref } from 'vue';

export interface AppearanceSettings {
  skin: 'aurora' | 'newsprint';
  mode: 'light' | 'dark';
  accent: string;
  compactList: boolean;
  lyricAlign: 'left' | 'center';
}

const TOKEN_ACCENTS = {
  aurora: {
    light: '#18875b',
    dark: '#62d6a2',
  },
  newsprint: {
    light: '#a8311b',
    dark: '#c4391e',
  },
} as const;

const DEFAULTS: AppearanceSettings = {
  skin: 'aurora',
  mode: 'light',
  accent: TOKEN_ACCENTS.aurora.light,
  compactList: false,
  lyricAlign: 'left',
};

const STORAGE_KEYS = {
  skin: 'appearance_skin',
  mode: 'appearance_mode',
  accent: 'appearance_accent',
  compactList: 'appearance_compact_list',
  lyricAlign: 'appearance_lyric_align',
} as const;

const LEGACY_KEYS = {
  skin: 'tweak_skin',
  mode: 'tweak_mode',
  accent: 'tweak_accent',
  compactList: 'tweak_compact',
  lyricAlign: 'tweak_lyric_align',
} as const;

const skin = ref<AppearanceSettings['skin']>(DEFAULTS.skin);
const mode = ref<AppearanceSettings['mode']>(DEFAULTS.mode);
const accent = ref(DEFAULTS.accent);
const compactList = ref(DEFAULTS.compactList);
const lyricAlign = ref<AppearanceSettings['lyricAlign']>(DEFAULTS.lyricAlign);
const customAccent = ref<string | null>(null);

let initialized = false;

function isSkin(value: string | null): value is AppearanceSettings['skin'] {
  return value === 'aurora' || value === 'newsprint';
}

function isMode(value: string | null): value is AppearanceSettings['mode'] {
  return value === 'light' || value === 'dark';
}

function isAccent(value: string | null): value is string {
  return Boolean(value && /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(value));
}

function isCompactList(value: string | null): value is 'true' | 'false' {
  return value === 'true' || value === 'false';
}

function isLyricAlign(value: string | null): value is AppearanceSettings['lyricAlign'] {
  return value === 'left' || value === 'center';
}

function tokenAccent(): string {
  return TOKEN_ACCENTS[skin.value][mode.value];
}

function syncAccentRef() {
  accent.value = customAccent.value ?? tokenAccent();
}

function applyToDom() {
  const root = document.documentElement;
  root.dataset.skin = skin.value;
  root.dataset.mode = mode.value;
  root.dataset.compactList = String(compactList.value);
  root.dataset.lyricAlign = lyricAlign.value;
  root.classList.toggle('compact', compactList.value);
  root.classList.toggle('lyric-left', lyricAlign.value === 'left');
  if (customAccent.value) {
    root.style.setProperty('--accent', customAccent.value);
  } else {
    root.style.removeProperty('--accent');
  }
}

function readStoredValue(key: keyof typeof STORAGE_KEYS): string | null {
  const stored = localStorage.getItem(STORAGE_KEYS[key]);
  if (stored !== null || !(key in LEGACY_KEYS)) return stored;
  return localStorage.getItem(LEGACY_KEYS[key as keyof typeof LEGACY_KEYS]);
}

function persist(key: keyof typeof STORAGE_KEYS, value: string) {
  localStorage.setItem(STORAGE_KEYS[key], value);
}

export function useAppearanceStore() {
  return {
    skin,
    mode,
    accent,
    compactList,
    lyricAlign,
    setSkin(value: AppearanceSettings['skin']) {
      skin.value = isSkin(value) ? value : DEFAULTS.skin;
      syncAccentRef();
      persist('skin', skin.value);
      applyToDom();
    },
    setMode(value: AppearanceSettings['mode']) {
      mode.value = isMode(value) ? value : DEFAULTS.mode;
      syncAccentRef();
      persist('mode', mode.value);
      applyToDom();
    },
    setAccent(value: string) {
      if (isAccent(value)) {
        customAccent.value = value;
        accent.value = value;
        persist('accent', value);
      } else {
        customAccent.value = null;
        syncAccentRef();
        localStorage.removeItem(STORAGE_KEYS.accent);
      }
      applyToDom();
    },
    setCompactList(value: boolean) {
      compactList.value = typeof value === 'boolean' ? value : DEFAULTS.compactList;
      persist('compactList', String(compactList.value));
      applyToDom();
    },
    setLyricAlign(value: AppearanceSettings['lyricAlign']) {
      lyricAlign.value = isLyricAlign(value) ? value : DEFAULTS.lyricAlign;
      persist('lyricAlign', lyricAlign.value);
      applyToDom();
    },
    init() {
      if (initialized) return;
      initialized = true;
      const storedSkin = readStoredValue('skin');
      const storedMode = readStoredValue('mode');
      const storedAccent = readStoredValue('accent');
      const storedCompactList = readStoredValue('compactList');
      const storedLyricAlign = readStoredValue('lyricAlign');

      skin.value = isSkin(storedSkin) ? storedSkin : DEFAULTS.skin;
      mode.value = isMode(storedMode) ? storedMode : DEFAULTS.mode;
      customAccent.value = isAccent(storedAccent) ? storedAccent : null;
      syncAccentRef();
      compactList.value = isCompactList(storedCompactList)
        ? storedCompactList === 'true'
        : DEFAULTS.compactList;
      lyricAlign.value = isLyricAlign(storedLyricAlign)
        ? storedLyricAlign
        : DEFAULTS.lyricAlign;
      applyToDom();
    },
  };
}

/** Test-only reset so each store contract test starts from the defaults. */
export function __resetForTest() {
  initialized = false;
  customAccent.value = null;
  skin.value = DEFAULTS.skin;
  mode.value = DEFAULTS.mode;
  accent.value = DEFAULTS.accent;
  compactList.value = DEFAULTS.compactList;
  lyricAlign.value = DEFAULTS.lyricAlign;
  const root = document.documentElement;
  root.removeAttribute('data-skin');
  root.removeAttribute('data-mode');
  root.removeAttribute('data-compact-list');
  root.removeAttribute('data-lyric-align');
  root.classList.remove('compact', 'lyric-left');
  root.style.removeProperty('--accent');
}

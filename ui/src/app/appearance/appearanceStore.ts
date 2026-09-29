import { ref } from 'vue';
import { safeReadItem, safeRemoveItem, safeSetItem } from '../../platform/storage/safeStorage';

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

/**
 * Writes we could not land in storage (quota exceeded, private mode, storage
 * disabled). Appearance is *not* lost: the values are live in memory and in the
 * DOM, this map only records that persistence is behind so a later retry can
 * catch storage up. `null` value means "this key must be absent".
 */
const pendingPersist = new Map<keyof typeof STORAGE_KEYS, string | null>();

/** Fields whose startup read failed and still need a retry. */
const pendingRead = new Set<keyof typeof STORAGE_KEYS>();

/** True while a storage read or write still needs retrying. */
const storageDegraded = ref(false);

function syncDegraded(): void {
  storageDegraded.value = pendingPersist.size > 0 || pendingRead.size > 0;
}

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

function readStoredValue(key: keyof typeof STORAGE_KEYS) {
  const stored = safeReadItem(STORAGE_KEYS[key]);
  // A failed canonical read is not evidence that the canonical key is absent;
  // defer the legacy lookup until a retry so canonical precedence stays intact.
  if (!stored.ok || stored.value !== null || !(key in LEGACY_KEYS)) return stored;
  return safeReadItem(LEGACY_KEYS[key as keyof typeof LEGACY_KEYS]);
}

function applyStoredValue(key: keyof typeof STORAGE_KEYS, stored: string | null): void {
  switch (key) {
    case 'skin':
      skin.value = isSkin(stored) ? stored : DEFAULTS.skin;
      break;
    case 'mode':
      mode.value = isMode(stored) ? stored : DEFAULTS.mode;
      break;
    case 'accent':
      customAccent.value = isAccent(stored) ? stored : null;
      break;
    case 'compactList':
      compactList.value = isCompactList(stored)
        ? stored === 'true'
        : DEFAULTS.compactList;
      break;
    case 'lyricAlign':
      lyricAlign.value = isLyricAlign(stored) ? stored : DEFAULTS.lyricAlign;
      break;
  }
}

/** Retry startup reads, applying only fields that no setter has claimed. */
function retryPendingReads(): void {
  let applied = false;
  for (const key of [...pendingRead]) {
    const result = readStoredValue(key);
    if (!result.ok) continue;
    pendingRead.delete(key);
    applyStoredValue(key, result.value);
    applied = true;
  }
  if (applied) {
    syncAccentRef();
    applyToDom();
  }
  syncDegraded();
}

function markReadResolvedByUser(key: keyof typeof STORAGE_KEYS): void {
  pendingRead.delete(key);
}

/**
 * Land a value in storage, or remember it as pending. Never throws.
 *
 * Callers must apply the value to refs + DOM *before* calling this, so a
 * storage failure can never leave the rendered skin behind the selected one —
 * that mismatch used to make Aurora and Newsprint styles mix after a
 * QuotaExceededError.
 */
function persist(key: keyof typeof STORAGE_KEYS, value: string | null): void {
  const landed = value === null
    ? safeRemoveItem(STORAGE_KEYS[key])
    : safeSetItem(STORAGE_KEYS[key], value);
  if (landed) {
    pendingPersist.delete(key);
  } else {
    pendingPersist.set(key, value);
  }
  syncDegraded();
}

/** Retry every write that failed earlier. Returns true when storage is back in sync. */
function flushPendingPersist(): boolean {
  for (const [key, value] of [...pendingPersist.entries()]) {
    const landed = value === null
      ? safeRemoveItem(STORAGE_KEYS[key])
      : safeSetItem(STORAGE_KEYS[key], value);
    if (!landed) continue;
    pendingPersist.delete(key);
  }
  syncDegraded();
  return pendingPersist.size === 0;
}

export function useAppearanceStore() {
  return {
    skin,
    mode,
    accent,
    compactList,
    lyricAlign,
    setSkin(value: AppearanceSettings['skin']) {
      markReadResolvedByUser('skin');
      skin.value = isSkin(value) ? value : DEFAULTS.skin;
      syncAccentRef();
      applyToDom();
      persist('skin', skin.value);
    },
    setMode(value: AppearanceSettings['mode']) {
      markReadResolvedByUser('mode');
      mode.value = isMode(value) ? value : DEFAULTS.mode;
      syncAccentRef();
      applyToDom();
      persist('mode', mode.value);
    },
    setAccent(value: string) {
      markReadResolvedByUser('accent');
      if (isAccent(value)) {
        customAccent.value = value;
        accent.value = value;
        applyToDom();
        persist('accent', value);
      } else {
        customAccent.value = null;
        syncAccentRef();
        applyToDom();
        persist('accent', null);
      }
    },
    setCompactList(value: boolean) {
      markReadResolvedByUser('compactList');
      compactList.value = typeof value === 'boolean' ? value : DEFAULTS.compactList;
      applyToDom();
      persist('compactList', String(compactList.value));
    },
    setLyricAlign(value: AppearanceSettings['lyricAlign']) {
      markReadResolvedByUser('lyricAlign');
      lyricAlign.value = isLyricAlign(value) ? value : DEFAULTS.lyricAlign;
      applyToDom();
      persist('lyricAlign', lyricAlign.value);
    },
    /** Reactive flag for UI: a persisted value is unreadable or a write is pending. */
    storageDegraded,
    /** Retry failed startup reads and writes; true when appearance is back in sync. */
    retryStoragePersistence(): boolean {
      retryPendingReads();
      flushPendingPersist();
      return pendingRead.size === 0 && pendingPersist.size === 0;
    },
    init() {
      if (initialized) return;
      initialized = true;
      // A hostile host (storage disabled / SecurityError on the getter) must not
      // take the app down: reads degrade to null and every value below falls
      // back to its default, then the DOM is synced to exactly that state.
      for (const key of Object.keys(STORAGE_KEYS) as (keyof typeof STORAGE_KEYS)[]) {
        const result = readStoredValue(key);
        if (result.ok) {
          applyStoredValue(key, result.value);
        } else {
          pendingRead.add(key);
        }
      }
      syncAccentRef();
      applyToDom();
      syncDegraded();
      // A previous in-session write may have failed for a transient reason; give
      // it one more chance so storage does not stay silently stale.
      flushPendingPersist();
    },
  };
}

/** Test-only reset so each store contract test starts from the defaults. */
export function __resetForTest() {
  initialized = false;
  customAccent.value = null;
  pendingPersist.clear();
  pendingRead.clear();
  storageDegraded.value = false;
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

import { ref, type Ref } from 'vue';
import { safeGetItem, safeSetItem } from '../../platform/storage/safeStorage';

export type LyricFocusMode = 'readable' | 'stage';

const STORAGE_KEY = 'tweak_lyric_focus';
const DEFAULT_MODE: LyricFocusMode = 'readable';

const mode = ref<LyricFocusMode>(DEFAULT_MODE);
let initialized = false;

function isLyricFocusMode(value: string | null): value is LyricFocusMode {
  return value === 'readable' || value === 'stage';
}

function applyToDom() {
  document.documentElement.dataset.lyricFocus = mode.value;
}

export function useLyricFocusStore(): {
  mode: Ref<LyricFocusMode>;
  setMode: (m: LyricFocusMode) => void;
  toggle: () => void;
  init: () => void;
} {
  return {
    mode,
    setMode(m) {
      mode.value = m;
      applyToDom();
      safeSetItem(STORAGE_KEY, m);
    },
    toggle() {
      const next: LyricFocusMode = mode.value === 'readable' ? 'stage' : 'readable';
      mode.value = next;
      applyToDom();
      safeSetItem(STORAGE_KEY, next);
    },
    init() {
      if (initialized) return;
      initialized = true;
      const stored = safeGetItem(STORAGE_KEY);
      mode.value = isLyricFocusMode(stored) ? stored : DEFAULT_MODE;
      applyToDom();
    },
  };
}

/** Test-only: reset the initialized flag so tests can re-init. */
export function __resetLyricFocusForTest(): void {
  initialized = false;
  mode.value = DEFAULT_MODE;
}

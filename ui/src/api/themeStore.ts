import { ref } from 'vue';

export type SkinId = 'aurora' | 'newsprint';
export type Mode = 'light' | 'dark';

const skinId = ref<SkinId>('aurora');
const mode = ref<Mode>('light');

let initialized = false;

function applyToDom() {
  document.documentElement.dataset.skin = skinId.value;
  document.documentElement.dataset.mode = mode.value;
}

export function useThemeStore() {
  return {
    skinId,
    mode,
    setSkin(id: SkinId) {
      skinId.value = id;
      localStorage.setItem('tweak_skin', id);
      applyToDom();
    },
    setMode(m: Mode) {
      mode.value = m;
      localStorage.setItem('tweak_mode', m);
      applyToDom();
    },
    init() {
      if (initialized) return;
      initialized = true;
      const storedSkin = localStorage.getItem('tweak_skin') as SkinId | null;
      const storedMode = localStorage.getItem('tweak_mode') as Mode | null;
      skinId.value = storedSkin || 'aurora';
      mode.value = storedMode || 'light';
      applyToDom();
    },
  };
}

/** Test-only: reset the initialized flag so tests can re-init. */
export function __resetForTest() {
  initialized = false;
  skinId.value = 'aurora';
  mode.value = 'light';
}

import {
  __resetForTest as resetAppearance,
  useAppearanceStore,
  type AppearanceSettings,
} from './appearanceStore';

export type SkinId = AppearanceSettings['skin'];
export type Mode = AppearanceSettings['mode'];

const appearanceStore = useAppearanceStore();
const skinId = appearanceStore.skin;
const mode = appearanceStore.mode;

export function useThemeStore() {
  return {
    skinId,
    mode,
    setSkin: appearanceStore.setSkin,
    setMode: appearanceStore.setMode,
    init: appearanceStore.init,
  };
}

/** Test-only: reset the initialized flag so tests can re-init. */
export function __resetForTest() {
  resetAppearance();
}

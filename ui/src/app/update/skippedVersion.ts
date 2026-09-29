// Shared reactive tracker for the "skipped update version" so the sidebar
// badge clears immediately when the user clicks "跳过此版本" in Settings,
// without requiring a remount or restart.
//
// `typeof localStorage !== 'undefined'` is NOT protection against a throwing
// `localStorage` getter (SecurityError when site data is blocked), and this
// module-eval read used to break the whole app boot on such hosts. All access
// goes through safeStorage; the in-memory ref is the fallback value.
import { ref } from 'vue';
import { safeGetItem, safeRemoveItem, safeSetItem } from '../../platform/storage/safeStorage';

const STORAGE_KEY = 'tweak_skipped_version';
const _skipped = ref<string | null>(safeGetItem(STORAGE_KEY));

export function getSkippedVersion() {
  // Storage wins when it has a value; the ref is the fallback for hosts where
  // storage is unavailable or where a write could not land.
  const stored = safeGetItem(STORAGE_KEY);
  return stored !== null ? stored : _skipped.value;
}

export function setSkippedVersion(version: string | null) {
  if (version) {
    safeSetItem(STORAGE_KEY, version);
  } else {
    safeRemoveItem(STORAGE_KEY);
  }
  _skipped.value = version;
}

export function useSkippedVersion() {
  return _skipped;
}

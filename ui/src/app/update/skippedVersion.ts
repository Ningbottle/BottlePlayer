// Shared reactive tracker for the "skipped update version" so the sidebar
// badge clears immediately when the user clicks "跳过此版本" in Settings,
// without requiring a remount or restart.
import { ref } from 'vue';

const STORAGE_KEY = 'tweak_skipped_version';
const _skipped = ref<string | null>(typeof localStorage !== 'undefined' ? localStorage.getItem(STORAGE_KEY) : null);

export function getSkippedVersion() {
  if (typeof localStorage !== 'undefined') {
    return localStorage.getItem(STORAGE_KEY);
  }
  return _skipped.value;
}

export function setSkippedVersion(version: string | null) {
  if (typeof localStorage !== 'undefined') {
    if (version) {
      localStorage.setItem(STORAGE_KEY, version);
    } else {
      localStorage.removeItem(STORAGE_KEY);
    }
  }
  _skipped.value = version;
}

export function useSkippedVersion() {
  return _skipped;
}

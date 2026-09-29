/**
 * Single entry point for Web Storage access.
 *
 * Every helper here is total: it never throws, no matter how hostile the host
 * is. `localStorage` itself is a *throwing getter* — with cookies/site data
 * blocked, WebView storage disabled, or under certain privacy extensions, even
 * reading `window.localStorage` raises SecurityError. Code that touched the
 * property directly therefore crashed at module-eval or page-setup time (the
 * whole page goes blank, not just the feature), which is why nothing else in
 * the app is allowed to name `localStorage` any more.
 */
type WebStorage = Storage;

export type StorageReadResult =
  | { ok: true; value: string | null }
  | { ok: false };

/**
 * Resolve the shared localStorage object, or null when it is unavailable.
 * The property access is inside the try on purpose: the getter can throw.
 */
export function getLocalStorage(): WebStorage | null {
  try {
    if (typeof window === 'undefined') return null;
    const store = window.localStorage;
    return store ?? null;
  } catch {
    return null;
  }
}

/** True when a trivial write actually round-trips (quota is checked per-write, not here). */
export function isStorageWritable(): boolean {
  const store = getLocalStorage();
  if (!store) return false;
  const probeKey = 'bottlemusic_storage_probe';
  try {
    store.setItem(probeKey, '1');
    store.removeItem(probeKey);
    return true;
  } catch {
    return false;
  }
}

/** Safe localStorage string read that distinguishes a missing key from a failed read. */
export function safeReadItem(key: string): StorageReadResult {
  const store = getLocalStorage();
  if (!store) return { ok: false };
  try {
    return { ok: true, value: store.getItem(key) };
  } catch {
    return { ok: false };
  }
}

/** Safe localStorage string read. Returns null when storage is unavailable or throws. */
export function safeGetItem(key: string): string | null {
  const result = safeReadItem(key);
  return result.ok ? result.value : null;
}

/** Best-effort localStorage write. Returns false instead of throwing. */
export function safeSetItem(key: string, value: string): boolean {
  const store = getLocalStorage();
  if (!store) return false;
  try {
    store.setItem(key, value);
    return true;
  } catch {
    return false;
  }
}

/** Best-effort localStorage delete. Returns false instead of throwing. */
export function safeRemoveItem(key: string): boolean {
  const store = getLocalStorage();
  if (!store) return false;
  try {
    store.removeItem(key);
    return true;
  } catch {
    return false;
  }
}

/** Safe localStorage number: NaN/non-finite → fallback; out-of-range → clamp. */
export function loadNumber(key: string, fallback: number, min: number, max: number): number {
  const n = parseFloat(safeGetItem(key) ?? '');
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, n));
}

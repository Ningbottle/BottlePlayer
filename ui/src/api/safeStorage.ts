/**
 * Safe localStorage string read. Returns null if localStorage access throws
 * (WebView storage disabled / permission denied / SecurityError on file://).
 * Never throws — safe to call at module-eval time.
 */
export function safeGetItem(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

/** Safe localStorage number: NaN/non-finite → fallback; out-of-range → clamp. */
export function loadNumber(key: string, fallback: number, min: number, max: number): number {
  const n = parseFloat(safeGetItem(key) ?? '');
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, n));
}

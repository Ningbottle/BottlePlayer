/** Safe localStorage number: NaN/non-finite → fallback; out-of-range → clamp. */
export function loadNumber(key: string, fallback: number, min: number, max: number): number {
  const n = parseFloat(localStorage.getItem(key) ?? '');
  if (!Number.isFinite(n)) return fallback;
  return Math.min(max, Math.max(min, n));
}

import type { Track } from './normalizer';
import { safeGetItem, loadNumber, safeSetItem } from '../platform/storage/safeStorage';

// ── Volume persistence (single owner since Task B4) ──
// The Backend and the Store must never read or write player_volume directly;
// both go through this module.

export const PLAYER_VOLUME_KEY = 'player_volume';

export function loadPlayerVolume(): number {
  return loadNumber(PLAYER_VOLUME_KEY, 0.7, 0, 1);
}

export function savePlayerVolume(volume: number): void {
  safeSetItem(PLAYER_VOLUME_KEY, String(volume));
}

export function loadJSON<T>(key: string, fallback: T): T {
  try {
    const raw = safeGetItem(key);
    if (raw == null) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

export type QueueSnapshot = {
  queue: Track[];
  currentIndex: number;
};

type QueueGetter = () => QueueSnapshot;

// Atomic snapshot key: queue + currentIndex are persisted as a single JSON
// blob in one setItem call, so a quota/permission/WebView error cannot leave
// a half-written (queue-without-matching-index) snapshot on disk.
const SNAPSHOT_KEY = 'player_queue_snapshot';

let getSnapshot: QueueGetter | null = null;
let saveQueueTimer: ReturnType<typeof setTimeout> | null = null;
const SAVE_QUEUE_DEBOUNCE_MS = 500;

/** Wire after playerStore exists (avoids circular init). */
export function bindQueuePersistence(getter: QueueGetter): void {
  getSnapshot = getter;
}

function normalizeCurrentIndex(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) && Number.isInteger(value)
    ? value
    : -1;
}

function parseLegacyCurrentIndex(value: string | null): number {
  if (value == null || value.trim() === '') return -1;
  return normalizeCurrentIndex(Number(value));
}

/**
 * Read the persisted queue snapshot. Falls back to legacy `player_queue` +
 * `player_index` keys on first run after upgrade, so existing sessions
 * migrate transparently. Returns { queue: [], currentIndex: -1 } if nothing
 * is stored.
 */
export function loadQueueSnapshot(): QueueSnapshot {
  try {
    const raw = safeGetItem(SNAPSHOT_KEY);
    if (raw != null) {
      const parsed = JSON.parse(raw) as Partial<QueueSnapshot>;
      if (Array.isArray(parsed.queue)) {
        return {
          queue: parsed.queue,
          currentIndex: normalizeCurrentIndex(parsed.currentIndex),
        };
      }
    }
  } catch {
    // fall through to legacy keys
  }
  // Legacy migration: read the pre-refactor split keys. On the next successful
  // flush the combined snapshot key is populated and the legacy keys are
  // ignored thereafter. Both reads are safeGetItem — never throw even if
  // localStorage access is unavailable (WebView storage disabled / permission).
  return {
    queue: loadJSON<Track[]>('player_queue', []),
    currentIndex: parseLegacyCurrentIndex(safeGetItem('player_index')),
  };
}

/**
 * Synchronously flush the pending debounced queue save (if any) and persist
 * the current queue snapshot. Atomic single-key write — never throws.
 *
 * Returns true on success, false on failure. Callers (HMR cleanup, pagehide,
 * and the saveQueue debounce timer callback) are safe even if localStorage
 * is unavailable or over quota.
 */
export function flushSaveQueue(): boolean {
  if (saveQueueTimer != null) {
    clearTimeout(saveQueueTimer);
    saveQueueTimer = null;
  }
  if (!getSnapshot) return false;
  try {
    const { queue, currentIndex } = getSnapshot();
    localStorage.setItem(SNAPSHOT_KEY, JSON.stringify({ queue, currentIndex }));
    return true;
  } catch {
    // localStorage.setItem failed (quota / permission / WebView storage
    // error). Best-effort: return false, do not throw. Teardown and the
    // debounce callback must not be broken by persistence failure.
    return false;
  }
}

export function saveQueue(): void {
  if (saveQueueTimer != null) clearTimeout(saveQueueTimer);
  saveQueueTimer = setTimeout(() => {
    saveQueueTimer = null;
    // flushSaveQueue is guaranteed non-throwing, so this debounce callback
    // is safe even if localStorage.setItem fails.
    flushSaveQueue();
  }, SAVE_QUEUE_DEBOUNCE_MS);
}

// R4: the module-top-level `beforeunload` listener was removed.
// `pagehide` → `disposePlayerRuntime()` → `flushSaveQueue()` is the single
// flush owner (bound in initPlayer, so only the live module owns it). The old
// listener was redundant with pagehide and registered at module top level,
// meaning orphan HMR modules also registered it.

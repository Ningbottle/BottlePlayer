import type { Track } from './normalizer';

export function loadJSON<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key);
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

let getSnapshot: QueueGetter | null = null;
let saveQueueTimer: ReturnType<typeof setTimeout> | null = null;
const SAVE_QUEUE_DEBOUNCE_MS = 500;

/** Wire after playerStore exists (avoids circular init). */
export function bindQueuePersistence(getter: QueueGetter): void {
  getSnapshot = getter;
}

export function flushSaveQueue(): void {
  if (saveQueueTimer != null) {
    clearTimeout(saveQueueTimer);
    saveQueueTimer = null;
  }
  if (!getSnapshot) return;
  const { queue, currentIndex } = getSnapshot();
  localStorage.setItem('player_queue', JSON.stringify(queue));
  localStorage.setItem('player_index', String(currentIndex));
}

export function saveQueue(): void {
  if (saveQueueTimer != null) clearTimeout(saveQueueTimer);
  saveQueueTimer = setTimeout(() => {
    saveQueueTimer = null;
    flushSaveQueue();
  }, SAVE_QUEUE_DEBOUNCE_MS);
}

if (typeof window !== 'undefined') {
  window.addEventListener('beforeunload', () => {
    flushSaveQueue();
  });
}

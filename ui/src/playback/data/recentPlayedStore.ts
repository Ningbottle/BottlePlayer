import { ref } from 'vue';
import type { Track } from '../../shared/music/track';
import { getLocalStorage } from '../../platform/storage/safeStorage';

export interface RecentPlayedEntry {
  FileHash: string;
  SongName: string;
  SingerName: string;
  AlbumName?: string;
  AlbumID?: string;
  Image?: string;
  Duration: number;
  playedAt: number;
}

export interface RecentPlayedStoreOptions {
  now?: () => number;
  storage?: Storage;
  storageKey?: string;
}

const DEFAULT_STORAGE_KEY = 'recent_played';
const MAX_RECENT_ENTRIES = 100;

function isRecentPlayedEntry(value: unknown): value is RecentPlayedEntry {
  if (!value || typeof value !== 'object') return false;
  const entry = value as Partial<RecentPlayedEntry>;
  return typeof entry.FileHash === 'string' && entry.FileHash.trim().length > 0
    && typeof entry.SongName === 'string'
    && typeof entry.SingerName === 'string'
    && typeof entry.Duration === 'number' && Number.isFinite(entry.Duration) && entry.Duration >= 0
    && typeof entry.playedAt === 'number' && Number.isFinite(entry.playedAt) && entry.playedAt >= 0;
}

function hashKey(hash: string): string { return hash.trim().toLowerCase(); }

function loadEntries(storage: Storage, key: string): RecentPlayedEntry[] {
  try {
    const raw = storage.getItem(key);
    if (raw == null) return [];
    const parsed: unknown = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed.filter(isRecentPlayedEntry).slice(0, MAX_RECENT_ENTRIES) : [];
  } catch {
    return [];
  }
}

export class RecentPlayedStore {
  readonly entries = ref<RecentPlayedEntry[]>([]);
  private readonly now: () => number;
  private readonly storage: Storage | undefined;
  private readonly storageKey: string;

  constructor(opts: RecentPlayedStoreOptions = {}) {
    this.now = opts.now ?? Date.now;
    this.storage = opts.storage;
    this.storageKey = opts.storageKey ?? DEFAULT_STORAGE_KEY;
    if (this.storage) {
      this.entries.value = loadEntries(this.storage, this.storageKey);
    }
  }

  recordRecentPlayed(track: Track): void {
    if (!track.FileHash?.trim()) return;
    const entry: RecentPlayedEntry = {
      FileHash: track.FileHash,
      SongName: track.SongName,
      SingerName: track.SingerName,
      AlbumName: track.AlbumName,
      AlbumID: track.AlbumID,
      Image: track.Image,
      Duration: track.Duration,
      playedAt: this.now(),
    };
    const rest = this.entries.value.filter((e) => hashKey(e.FileHash) !== hashKey(track.FileHash));
    this.entries.value = [entry, ...rest].slice(0, MAX_RECENT_ENTRIES);
    this.persist();
  }

  /**
   * Merge local entries with remote entries, deduping by FileHash (latest
   * playedAt wins), sorted desc. Pure: does NOT mutate the local store or
   * persist remote entries to storage. The caller (HistoryView) owns the
   * merged display list.
   */
  mergeRemote(remoteEntries: RecentPlayedEntry[]): RecentPlayedEntry[] {
    const byHash = new Map<string, RecentPlayedEntry>();
    for (const entry of [...this.entries.value, ...remoteEntries]) {
      if (!isRecentPlayedEntry(entry)) continue;
      const key = hashKey(entry.FileHash);
      const existing = byHash.get(key);
      if (!existing || entry.playedAt > existing.playedAt) {
        byHash.set(key, entry);
      }
    }
    return Array.from(byHash.values()).sort((a, b) => b.playedAt - a.playedAt);
  }

  /** Clear all entries (in-memory + persisted). Used on logout / test reset. */
  reset(): void {
    this.entries.value = [];
    this.persist();
  }

  private persist(): void {
    if (!this.storage) return;
    try {
      this.storage.setItem(this.storageKey, JSON.stringify(this.entries.value));
    } catch {
      // Persist failures (quota, private mode) must not break playback.
    }
  }
}

/**
 * Production singleton. Storage is resolved through safeStorage because the
 * `localStorage` property itself throws SecurityError on hosts with site data
 * blocked — naming it here used to break module evaluation for the whole
 * playback layer (and with it the app), not just the recent list.
 */
export const recentPlayedStore = new RecentPlayedStore({
  storage: getLocalStorage() ?? undefined,
});

import { describe, it, expect } from 'vitest';
import { RecentPlayedStore, type RecentPlayedEntry } from '../recentPlayedStore';
import type { Track } from '../normalizer';

function mkTrack(partial: Partial<Track> = {}): Track {
  return {
    FileHash: 'hash-A',
    SongName: 'Song A',
    SingerName: 'Artist A',
    Duration: 200,
    ...partial,
  };
}

/** In-memory Storage for hermetic tests (no jsdom localStorage pollution). */
function memStorage(): Storage {
  const map = new Map<string, string>();
  return {
    getItem: (k) => (map.has(k) ? map.get(k)! : null),
    setItem: (k, v) => void map.set(k, v),
    removeItem: (k) => void map.delete(k),
    clear: () => map.clear(),
    key: (i) => Array.from(map.keys())[i] ?? null,
    get length() {
      return map.size;
    },
  } as Storage;
}

function mkStore(opts: { now?: () => number; storage?: Storage } = {}) {
  return new RecentPlayedStore({
    now: opts.now ?? (() => 0),
    storage: opts.storage ?? memStorage(),
  });
}

describe('RecentPlayedStore', () => {
  it('records a new track at the top of the list (most recent first)', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1', SongName: 'First' }));

    expect(store.entries.value).toHaveLength(1);
    expect(store.entries.value[0].FileHash).toBe('h1');
    expect(store.entries.value[0].SongName).toBe('First');
    expect(store.entries.value[0].playedAt).toBe(1000);
  });

  it('re-recording an existing track moves it to top and updates playedAt without duplicating', () => {
    let now = 1000;
    const store = mkStore({ now: () => now });
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1', SongName: 'First' }));
    store.recordRecentPlayed(mkTrack({ FileHash: 'h2', SongName: 'Second' }));
    now = 2000;
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1', SongName: 'First (replay)' }));

    expect(store.entries.value).toHaveLength(2);
    expect(store.entries.value[0].FileHash).toBe('h1');
    expect(store.entries.value[0].playedAt).toBe(2000);
    expect(store.entries.value[0].SongName).toBe('First (replay)');
    expect(store.entries.value[1].FileHash).toBe('h2');
  });

  it('persists entries to storage and reloads on construction', () => {
    const storage = memStorage();
    const store = mkStore({ now: () => 5000, storage });
    store.recordRecentPlayed(mkTrack({ FileHash: 'h-persist', SongName: 'Saved' }));

    // A new store instance with the same storage must see the saved entry.
    const reloaded = mkStore({ now: () => 9999, storage });
    expect(reloaded.entries.value).toHaveLength(1);
    expect(reloaded.entries.value[0].FileHash).toBe('h-persist');
    expect(reloaded.entries.value[0].SongName).toBe('Saved');
    expect(reloaded.entries.value[0].playedAt).toBe(5000);
  });

  it('falls back to empty when storage data is corrupt (no throw)', () => {
    const storage = memStorage();
    storage.setItem('recent_played', 'not-json{');
    const store = mkStore({ now: () => 1, storage });
    expect(store.entries.value).toHaveLength(0);
    // Recording after corrupt load still works and overwrites the bad data.
    store.recordRecentPlayed(mkTrack({ FileHash: 'h-fix' }));
    expect(store.entries.value).toHaveLength(1);
    expect(storage.getItem('recent_played')).toContain('h-fix');
  });

  it('mergeRemote merges local+remote, dedupes by FileHash (latest playedAt wins), sorted desc, without mutating local', () => {
    let now = 0;
    const store = mkStore({ now: () => now });
    now = 1000;
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1', SongName: 'Local1' }));
    now = 2000;
    store.recordRecentPlayed(mkTrack({ FileHash: 'h2', SongName: 'Local2' }));
    // local after records: [h2@2000, h1@1000]

    const remote: RecentPlayedEntry[] = [
      { FileHash: 'h2', SongName: 'Remote2-newer', SingerName: '', Duration: 0, playedAt: 3000 },
      { FileHash: 'h3', SongName: 'Remote3', SingerName: '', Duration: 0, playedAt: 1500 },
    ];

    const merged = store.mergeRemote(remote);

    // Deduped: h2 appears once (remote@3000 wins over local@2000).
    expect(merged).toHaveLength(3);
    expect(merged.map((e) => e.FileHash)).toEqual(['h2', 'h3', 'h1']);
    expect(merged[0].playedAt).toBe(3000);
    expect(merged[0].SongName).toBe('Remote2-newer');
    expect(merged[2].playedAt).toBe(1000);

    // Local store state must NOT be mutated by the merge (remote not persisted).
    expect(store.entries.value).toHaveLength(2);
    expect(store.entries.value[0].FileHash).toBe('h2');
    expect(store.entries.value[0].playedAt).toBe(2000);
  });
});

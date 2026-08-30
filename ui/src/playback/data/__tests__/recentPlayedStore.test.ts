import { describe, it, expect } from 'vitest';
import { RecentPlayedStore, type RecentPlayedEntry } from '../recentPlayedStore';
import type { Track } from '../../../shared/music/track';

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

  it('falls back to empty when storage contains valid JSON with the wrong shape', () => {
    const storage = memStorage();
    storage.setItem('recent_played', JSON.stringify({ unexpected: true }));
    const store = mkStore({ now: () => 1, storage });

    expect(store.entries.value).toEqual([]);
    expect(() => store.recordRecentPlayed(mkTrack({ FileHash: 'h-shape' }))).not.toThrow();
    expect(store.entries.value).toHaveLength(1);
  });

  it('caps local history so repeated unique plays do not grow storage without bound', () => {
    const storage = memStorage();
    let now = 0;
    const store = mkStore({ now: () => now, storage });

    for (let index = 0; index < 101; index++) {
      now++;
      store.recordRecentPlayed(mkTrack({ FileHash: `h-${index}` }));
    }

    expect(store.entries.value).toHaveLength(100);
    expect(store.entries.value[0].FileHash).toBe('h-100');
    expect(store.entries.value[store.entries.value.length - 1]?.FileHash).toBe('h-1');
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

  it('mergeRemote with empty remote returns a sorted copy of local entries without mutating the store', () => {
    let now = 0;
    const store = mkStore({ now: () => now });
    now = 1000;
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1' }));
    now = 2000;
    store.recordRecentPlayed(mkTrack({ FileHash: 'h2' }));
    // local: [h2@2000, h1@1000]

    const before = [...store.entries.value];
    const merged = store.mergeRemote([]);

    expect(merged.map((e) => e.FileHash)).toEqual(['h2', 'h1']);
    expect(merged).not.toBe(store.entries.value); // new ref, not the internal ref
    expect(store.entries.value).toEqual(before); // store untouched
  });

  it('mergeRemote with empty local returns the remote entries sorted desc', () => {
    const store = mkStore({ now: () => 0 });
    const remote: RecentPlayedEntry[] = [
      { FileHash: 'h1', SongName: 'R1', SingerName: '', Duration: 0, playedAt: 1000 },
      { FileHash: 'h2', SongName: 'R2', SingerName: '', Duration: 0, playedAt: 3000 },
      { FileHash: 'h3', SongName: 'R3', SingerName: '', Duration: 0, playedAt: 2000 },
    ];

    const merged = store.mergeRemote(remote);

    expect(merged.map((e) => e.FileHash)).toEqual(['h2', 'h3', 'h1']);
    expect(store.entries.value).toHaveLength(0); // local still empty
  });

  it('mergeRemote dedupes duplicate FileHash within the remote input (latest playedAt wins)', () => {
    const store = mkStore({ now: () => 0 });
    const remote: RecentPlayedEntry[] = [
      { FileHash: 'h1', SongName: 'older', SingerName: '', Duration: 0, playedAt: 1000 },
      { FileHash: 'h1', SongName: 'newer', SingerName: '', Duration: 0, playedAt: 5000 },
      { FileHash: 'h1', SongName: 'middle', SingerName: '', Duration: 0, playedAt: 3000 },
    ];

    const merged = store.mergeRemote(remote);

    expect(merged).toHaveLength(1);
    expect(merged[0].FileHash).toBe('h1');
    expect(merged[0].SongName).toBe('newer');
    expect(merged[0].playedAt).toBe(5000);
  });

  it('mergeRemote does not write to storage (pure read)', () => {
    const storage = memStorage();
    const store = mkStore({ now: () => 1000, storage });
    store.recordRecentPlayed(mkTrack({ FileHash: 'h1' }));
    const storageBefore = storage.getItem('recent_played');

    store.mergeRemote([
      { FileHash: 'h2', SongName: 'R2', SingerName: '', Duration: 0, playedAt: 3000 },
    ]);

    expect(storage.getItem('recent_played')).toBe(storageBefore);
  });

  it('works without storage (storage undefined) without throwing', () => {
    const store = new RecentPlayedStore({ now: () => 1000 });
    expect(() => store.recordRecentPlayed(mkTrack({ FileHash: 'h1', SongName: 'No-storage' }))).not.toThrow();
    expect(store.entries.value).toHaveLength(1);
    expect(store.entries.value[0].FileHash).toBe('h1');

    // mergeRemote still works without storage.
    const merged = store.mergeRemote([
      { FileHash: 'h2', SongName: 'R', SingerName: '', Duration: 0, playedAt: 2000 },
    ]);
    expect(merged).toHaveLength(2);
  });

  it('swallows storage setItem failures (quota/private mode) without breaking recording', () => {
    const storage = memStorage();
    const original = storage.setItem;
    storage.setItem = () => {
      throw new Error('QuotaExceededError');
    };
    const store = mkStore({ now: () => 1000, storage });

    expect(() => store.recordRecentPlayed(mkTrack({ FileHash: 'h1' }))).not.toThrow();
    // In-memory state still updated even though persist failed.
    expect(store.entries.value).toHaveLength(1);
    expect(store.entries.value[0].FileHash).toBe('h1');

    storage.setItem = original;
  });

  it('honors a custom storageKey for persist and reload', () => {
    const storage = memStorage();
    const store = new RecentPlayedStore({ now: () => 1000, storage, storageKey: 'custom_key' });
    store.recordRecentPlayed(mkTrack({ FileHash: 'h-custom' }));

    expect(storage.getItem('recent_played')).toBeNull(); // default key untouched
    expect(storage.getItem('custom_key')).toContain('h-custom');

    const reloaded = new RecentPlayedStore({ now: () => 9999, storage, storageKey: 'custom_key' });
    expect(reloaded.entries.value).toHaveLength(1);
    expect(reloaded.entries.value[0].FileHash).toBe('h-custom');
  });

  it('recordRecentPlayed copies all track fields including optional Album/AlbumID/Image', () => {
    const store = mkStore({ now: () => 1000 });
    store.recordRecentPlayed(
      mkTrack({
        FileHash: 'h-full',
        SongName: 'Full Song',
        SingerName: 'Full Artist',
        AlbumName: 'Full Album',
        AlbumID: 'album-123',
        Image: 'https://img/400.jpg',
        Duration: 250,
      }),
    );

    expect(store.entries.value[0]).toEqual({
      FileHash: 'h-full',
      SongName: 'Full Song',
      SingerName: 'Full Artist',
      AlbumName: 'Full Album',
      AlbumID: 'album-123',
      Image: 'https://img/400.jpg',
      Duration: 250,
      playedAt: 1000,
    });
  });
});

import { describe, expect, it } from 'vitest';

import { resolveViewDescriptor, type HistoryEntry } from '../viewRegistry';

describe('view registry', () => {
  it('uses stable cache keys for home, playlists, and committed searches', () => {
    const home = resolveViewDescriptor({ view: 'home' });
    const playlist = resolveViewDescriptor({
      view: 'playlist',
      playlistId: 'playlist-42',
      playlistName: 'Evening Mix',
    });
    const search = resolveViewDescriptor({ view: 'search', searchQuery: 'jazz' });

    expect(home.cacheKey).toBe('home');
    expect(home.keepAlive).toBe(true);
    expect(playlist.cacheKey).toBe('playlist:playlist-42');
    expect(playlist.keepAlive).toBe(false);
    expect(search.cacheKey).toBe('search:jazz');
    expect(search.keepAlive).toBe(false);
  });

  it('keeps cache identity stable when the transition identity changes', () => {
    const firstEntry: HistoryEntry = { view: 'home', transitionKey: 'home:1' };
    const secondEntry: HistoryEntry = { view: 'home', transitionKey: 'home:2' };

    const first = resolveViewDescriptor(firstEntry);
    const second = resolveViewDescriptor(secondEntry);

    expect(first.transitionKey).not.toBe(second.transitionKey);
    expect(first.cacheKey).toBe('home');
    expect(second.cacheKey).toBe('home');
  });
});

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { flushPromises } from '@vue/test-utils';

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../backend', () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
  apiPost: (...args: unknown[]) => mockApiPost(...args),
}));

import { userStore } from '../userStore';
import {
  favoriteStore,
  __resetFavoriteStoreForTests,
} from '../favoriteStore';
import * as favoriteRepository from '../favoriteRepository';
import { __resetFavoriteRepositoryForTests } from '../favoriteRepository';
import type { Track } from '../normalizer';

function mkTrack(hash: string, audioId = ''): Track {
  return {
    FileHash: hash,
    SongName: hash,
    SingerName: 'A',
    Duration: 100,
    audio_id: audioId,
    album_audio_id: audioId,
  } as Track;
}

function likedPlaylistResponse(listid: string, userid: string) {
  return {
    status: 1,
    data: {
      info: [
        {
          global_collection_id: `collection_3_${userid}_${listid}_0`,
          listid,
          listname: '我喜欢的音乐',
          songcount: 0,
        },
        { global_collection_id: `collection_3_${userid}_other_0`, listid: 'other', listname: '通勤精选', songcount: 3 },
      ],
    },
  };
}

let currentUser = 'u1';

function setupLiked(tracks: Track[], total = tracks.length) {
  mockApiGet.mockImplementation((path: string, query?: Record<string, unknown>) => {
    if (path === '/user/playlist') {
      const listid = currentUser === 'uA' ? 'aL' : currentUser === 'uB' ? 'bL' : '999';
      return Promise.resolve(likedPlaylistResponse(listid, currentUser));
    }
    if (path === '/playlist/track/all') {
      const id = String(query?.id ?? '');
      const listid = currentUser === 'uA' ? 'aL' : currentUser === 'uB' ? 'bL' : '999';
      const useTracks = id.includes(listid) ? tracks : [];
      return Promise.resolve({
        status: 1,
        data: { list: useTracks, total: id.includes(listid) ? total : 0 },
      });
    }
    return Promise.resolve({ status: 1, data: {} });
  });
}

describe('favoriteStore', () => {
  beforeEach(() => {
    __resetFavoriteStoreForTests();
    __resetFavoriteRepositoryForTests();
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    userStore.isLoggedIn = true;
    userStore.userId = 'u1';
    currentUser = 'u1';
  });

  describe('reconcile + liked playlist resolution', () => {
    it('reconciles all pages of the liked playlist and unions FileHashes', async () => {
      const tracks = Array.from({ length: 120 }, (_, i) => mkTrack(`h${i}`, String(i + 1)));
      setupLiked(tracks, 120);

      await favoriteStore.onLogin('u1');

      expect(favoriteStore.isFavorite('h0')).toBe(true);
      expect(favoriteStore.isFavorite('h119')).toBe(true);
      expect(favoriteStore.isFavorite('h200')).toBe(false);
      expect(favoriteStore.loaded).toBe(true);
    });

    it('resolves the liked playlist by name on first run and persists its id', async () => {
      setupLiked([]);
      await favoriteStore.onLogin('u1');
      expect(favoriteStore.getLikedPlaylist()?.listid).toBe('999');

      // Second reconcile reuses the persisted id (no /user/playlist refetch).
      mockApiGet.mockClear();
      await favoriteStore.reconcile();
      expect(mockApiGet).not.toHaveBeenCalledWith('/user/playlist', expect.anything());
      expect(mockApiGet).toHaveBeenCalledWith('/playlist/track/all', expect.anything());
    });

    it('hydrates a single liked page incrementally (PlaylistView pagination sync)', async () => {
      setupLiked([]);
      await favoriteStore.onLogin('u1');

      favoriteStore.hydrateLikedPage([mkTrack('p1', '1'), mkTrack('p2', '2')]);
      expect(favoriteStore.isFavorite('p1')).toBe(true);
      expect(favoriteStore.isFavorite('p2')).toBe(true);

      // A later page adds more without clearing earlier ones.
      favoriteStore.hydrateLikedPage([mkTrack('p3', '3')]);
      expect(favoriteStore.isFavorite('p1')).toBe(true);
      expect(favoriteStore.isFavorite('p3')).toBe(true);
    });
  });

  describe('setFavorite (optimistic + operation ID)', () => {
    it('setFavorite(true) optimistically marks and confirms via the add adapter', async () => {
      setupLiked([]);
      await favoriteStore.onLogin('u1');
      mockApiPost.mockResolvedValue({ status: 1 });

      const track = mkTrack('new1', '77');
      await favoriteStore.setFavorite(track, true);

      expect(favoriteStore.isFavorite('new1')).toBe(true);
      expect(mockApiPost).toHaveBeenCalledWith(
        '/playlist/tracks/add',
        undefined,
        expect.objectContaining({ listid: '999' }),
      );
    });

    it('setFavorite(false) removes the track via the del adapter (numeric fileid)', async () => {
      setupLiked([]);
      await favoriteStore.onLogin('u1');
      // Seed the archive so the del has a fileid to send.
      favoriteStore.hydrateLikedPage([mkTrack('rm1', '55')]);
      expect(favoriteStore.isFavorite('rm1')).toBe(true);

      mockApiPost.mockResolvedValue({ status: 1 });
      await favoriteStore.setFavorite(mkTrack('rm1', '55'), false);

      expect(favoriteStore.isFavorite('rm1')).toBe(false);
      expect(mockApiPost).toHaveBeenCalledWith(
        '/playlist/tracks/del',
        undefined,
        expect.objectContaining({ listid: '999', fileids: '55' }),
      );
    });

    it('quick favorite-then-unfavorite: the older add response does not relight the heart', async () => {
      setupLiked([]);
      await favoriteStore.onLogin('u1');

      let resolveAdd!: (v: unknown) => void;
      let resolveDel!: (v: unknown) => void;
      const addP = new Promise((r) => { resolveAdd = r; });
      const delP = new Promise((r) => { resolveDel = r; });
      mockApiPost.mockImplementation((path: string) => {
        if (path === '/playlist/tracks/add') return addP;
        if (path === '/playlist/tracks/del') return delP;
        return Promise.resolve({ status: 1 });
      });

      const track = mkTrack('toggle1', '88');
      const pAdd = favoriteStore.setFavorite(track, true);
      expect(favoriteStore.isFavorite('toggle1')).toBe(true); // optimistic add
      const pDel = favoriteStore.setFavorite(track, false);
      expect(favoriteStore.isFavorite('toggle1')).toBe(false); // optimistic del

      // Del confirms first.
      resolveDel({ status: 1 });
      await pDel;
      expect(favoriteStore.isFavorite('toggle1')).toBe(false);

      // Add confirms later - must NOT relight.
      resolveAdd({ status: 1 });
      await pAdd;
      expect(favoriteStore.isFavorite('toggle1')).toBe(false);
    });
  });

  describe('offline outbox', () => {
    it('persists an offline favorite to the outbox and replays it on reconnect', async () => {
      // Dynamic mock: /playlist/tracks/add mutates the server-side liked set,
      // and /playlist/track/all reflects it - so a post-reconnect reconcile
      // sees the replayed op.
      const serverHashes = new Set<string>();
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') {
          const tracks = [...serverHashes].map((h) => mkTrack(h, h));
          return Promise.resolve({ status: 1, data: { list: tracks, total: tracks.length } });
        }
        return Promise.resolve({ status: 1, data: {} });
      });

      await favoriteStore.onLogin('u1');
      expect(favoriteStore.isFavorite('off1')).toBe(false);

      // Offline: the add cannot reach the server, so it lands in the outbox.
      mockApiPost.mockImplementation(() => Promise.reject(new Error('network down')));
      await favoriteStore.setFavorite(mkTrack('off1', '99'), true);

      expect(favoriteStore.isFavorite('off1')).toBe(true); // optimistic
      expect(favoriteStore.pendingOutbox).toBe(1);

      // Reconnect: the add succeeds (server records it), then reconcile
      // re-fetches and confirms.
      mockApiPost.mockImplementation((path: string) => {
        if (path === '/playlist/tracks/add') serverHashes.add('off1');
        return Promise.resolve({ status: 1 });
      });
      await favoriteStore.onOnline();

      expect(favoriteStore.isFavorite('off1')).toBe(true);
      expect(favoriteStore.pendingOutbox).toBe(0);
      expect(mockApiPost).toHaveBeenCalledWith(
        '/playlist/tracks/add',
        undefined,
        expect.objectContaining({ listid: '999' }),
      );
    });
  });

  describe('user switching', () => {
    it('clears the previous user favorites and loads the new user set on login switch', async () => {
      const aTracks = [mkTrack('a1', '1')];
      const bTracks = [mkTrack('b1', '2')];

      currentUser = 'uA';
      setupLiked(aTracks);
      await favoriteStore.onLogin('uA');
      expect(favoriteStore.isFavorite('a1')).toBe(true);

      currentUser = 'uB';
      setupLiked(bTracks);
      await favoriteStore.onLogin('uB');

      expect(favoriteStore.isFavorite('a1')).toBe(false); // cleared
      expect(favoriteStore.isFavorite('b1')).toBe(true); // loaded
    });

    it('onLogout clears favorites and liked playlist for the bound user', async () => {
      setupLiked([mkTrack('lo1', '1')]);
      await favoriteStore.onLogin('u1');
      expect(favoriteStore.isFavorite('lo1')).toBe(true);

      favoriteStore.onLogout();

      expect(favoriteStore.isFavorite('lo1')).toBe(false);
      expect(favoriteStore.loaded).toBe(false);
      expect(favoriteStore.getLikedPlaylist()).toBeNull();
    });
  });

  describe('account-epoch isolation (no cross-account pollution)', () => {
    it('discards user A reconcile response after switching to user B', async () => {
      let resolveATracks!: (v: unknown) => void;
      mockApiGet.mockImplementation((path: string, query?: Record<string, unknown>) => {
        if (path === '/user/playlist') {
          const listid = currentUser === 'uA' ? 'aL' : 'bL';
          return Promise.resolve(likedPlaylistResponse(listid, currentUser));
        }
        if (path === '/playlist/track/all') {
          const id = String(query?.id ?? '');
          if (id.includes('aL')) return new Promise((r) => { resolveATracks = r; });
          if (id.includes('bL')) {
            return Promise.resolve({ status: 1, data: { list: [mkTrack('b1', '2')], total: 1 } });
          }
          return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
        }
        return Promise.resolve({ status: 1, data: {} });
      });

      currentUser = 'uA';
      const aLogin = favoriteStore.onLogin('uA'); // A reconcile starts; track fetch pending
      await flushPromises();

      // Switch to B while A's fetch is in flight.
      currentUser = 'uB';
      await favoriteStore.onLogin('uB');
      expect(favoriteStore.isFavorite('b1')).toBe(true);
      expect(favoriteStore.isFavorite('a1')).toBe(false);

      // A's response arrives with a1 - must NOT pollute B's state.
      resolveATracks({ status: 1, data: { list: [mkTrack('a1', '1')], total: 1 } });
      await aLogin;
      await flushPromises();

      expect(favoriteStore.isFavorite('a1')).toBe(false);
      expect(favoriteStore.isFavorite('b1')).toBe(true);
    });

    it('does not write user A outbox op into user B after a mid-flight switch', async () => {
      currentUser = 'uA';
      setupLiked([mkTrack('a1', '1')]);
      await favoriteStore.onLogin('uA');

      // Start a setFavorite whose add is gated.
      let resolveAdd!: (v: unknown) => void;
      mockApiPost.mockImplementation(() => new Promise((r) => { resolveAdd = r; }));
      const setP = favoriteStore.setFavorite(mkTrack('a1', '1'), false); // unfavorite a1
      await flushPromises();

      // Switch to B while the del is in flight.
      currentUser = 'uB';
      mockApiPost.mockReset();
      mockApiGet.mockReset();
      setupLiked([mkTrack('b1', '2')]);
      await favoriteStore.onLogin('uB');
      expect(favoriteStore.isFavorite('b1')).toBe(true);

      // A's del resolves with a transport error - must NOT enqueue into B's outbox.
      resolveAdd(Promise.reject(new Error('network down')));
      await setP.catch(() => {});
      await flushPromises();

      expect(favoriteStore.pendingOutbox).toBe(0); // B's outbox untouched
      expect(favoriteStore.isFavorite('b1')).toBe(true);
    });
  });

  describe('outbox concurrency', () => {
    it('preserves new outbox ops added during a flushOutbox network wait', async () => {
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') {
          return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
        }
        return Promise.resolve({ status: 1, data: {} });
      });
      await favoriteStore.onLogin('u1');

      // op1 (X) queued offline.
      mockApiPost.mockRejectedValue(new Error('down'));
      await favoriteStore.setFavorite(mkTrack('X', '1'), true);
      expect(favoriteStore.pendingOutbox).toBe(1);

      // flushOutbox: op1's replay is gated.
      let resolveReplay!: (v: unknown) => void;
      mockApiPost.mockImplementation(() => new Promise((r) => { resolveReplay = r; }));
      const flushP = favoriteStore.flushOutbox();
      await flushPromises();

      // op2 (Y) queued while op1 is mid-replay.
      mockApiPost.mockImplementation(() => Promise.reject(new Error('down')));
      await favoriteStore.setFavorite(mkTrack('Y', '2'), true);
      expect(favoriteStore.pendingOutbox).toBe(2);

      // op1 replay succeeds.
      resolveReplay({ status: 1 });
      await flushP;

      // op2 must survive (not overwritten by flushOutbox's snapshot).
      expect(favoriteStore.pendingOutbox).toBe(1);
    });
  });

  describe('anonymous favorites (not logged in)', () => {
    it('persists anonymous favorites locally and migrates them on login', async () => {
      userStore.isLoggedIn = false;
      userStore.userId = '';

      const result = await favoriteStore.setFavorite(mkTrack('anon1', '1'), true);
      expect(result.status).toBe('anonymous');
      expect(favoriteStore.isFavorite('anon1')).toBe(true);

      // Simulate a page reload: in-memory state lost, but local persistence survives.
      __resetFavoriteStoreForTests();
      expect(favoriteStore.isFavorite('anon1')).toBe(false);

      // Log in: the anonymous favorite migrates to the outbox and is replayed.
      userStore.isLoggedIn = true;
      userStore.userId = 'u1';
      const serverHashes = new Set<string>();
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') {
          const tracks = [...serverHashes].map((h) => mkTrack(h, h));
          return Promise.resolve({ status: 1, data: { list: tracks, total: tracks.length } });
        }
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockImplementation((path: string) => {
        if (path === '/playlist/tracks/add') {
          serverHashes.add('anon1');
          return Promise.resolve({ status: 1 });
        }
        return Promise.resolve({ status: 1 });
      });
      await favoriteStore.onLogin('u1');

      expect(favoriteStore.isFavorite('anon1')).toBe(true);
      expect(serverHashes.has('anon1')).toBe(true); // migrated op reached the server
    });

    it('migrates anonymous favorites to the outbox BEFORE clearing the source', async () => {
      userStore.isLoggedIn = false;
      userStore.userId = '';
      await favoriteStore.setFavorite(mkTrack('anon7', '1'), true);

      // Record the order of outbox writes vs the anonymous-source clear by
      // spying on the repository namespace (favoriteStore imports these as live
      // bindings, so the spy intercepts its calls).
      const order: string[] = [];
      const origSaveOutbox = favoriteRepository.saveOutbox;
      const origClearAnon = favoriteRepository.clearAnonymousFavorites;
      vi.spyOn(favoriteRepository, 'saveOutbox').mockImplementation((uid: string, ops) => {
        order.push('outbox-write');
        return origSaveOutbox(uid, ops);
      });
      vi.spyOn(favoriteRepository, 'clearAnonymousFavorites').mockImplementation(() => {
        order.push('anon-clear');
        origClearAnon();
      });

      userStore.isLoggedIn = true;
      userStore.userId = 'u1';
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockResolvedValue({ status: 1 });
      await favoriteStore.onLogin('u1');

      // The outbox must be persisted before the anonymous source is cleared,
      // so a crash/failed write between them cannot lose the favorites.
      expect(order).toContain('anon-clear');
      expect(order.indexOf('outbox-write')).toBeLessThan(order.indexOf('anon-clear'));
    });
  });

  describe('re-login safety', () => {
    it('same-account repeat login does not freeze a subsequent reconcile', async () => {
      let resolveTracks!: (v: unknown) => void;
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') return new Promise((r) => { resolveTracks = r; });
        return Promise.resolve({ status: 1, data: {} });
      });

      const first = favoriteStore.onLogin('u1'); // reconcile starts, gated
      await flushPromises();
      expect(favoriteStore.reconciling).toBe(true);

      // Same-account re-login while the first reconcile is in flight.
      const second = favoriteStore.onLogin('u1');
      await flushPromises();

      // Let the first reconcile finish.
      resolveTracks({ status: 1, data: { list: [], total: 0 } });
      await first;
      await second;

      // The reconcile flag must be released (not frozen by an epoch bump).
      expect(favoriteStore.reconciling).toBe(false);

      // A subsequent reconcile must actually run (fetch), not return a stale promise.
      mockApiGet.mockClear();
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/playlist/track/all') return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        return Promise.resolve({ status: 1, data: {} });
      });
      await favoriteStore.reconcile();
      expect(mockApiGet).toHaveBeenCalledWith('/playlist/track/all', expect.anything());
    });
  });

  describe('first-login offline favorites', () => {
    it('enters the outbox as pending when the liked playlist cannot be resolved (offline)', async () => {
      // Logged in, but /user/playlist is unreachable and no liked id is cached.
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.reject(new Error('network down'));
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockResolvedValue({ status: 1 });
      await favoriteStore.onLogin('u1'); // reconcile fails to resolve liked; that's fine
      expect(favoriteStore.getLikedPlaylist()).toBeNull();

      const result = await favoriteStore.setFavorite(mkTrack('off-first', '1'), true);
      expect(result.status).toBe('pending'); // NOT failed
      expect(favoriteStore.isFavorite('off-first')).toBe(true); // optimistic retained
      expect(favoriteStore.pendingOutbox).toBe(1);

      // On reconnect the liked playlist resolves and the queued op replays.
      const serverHashes = new Set<string>();
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') {
          const tracks = [...serverHashes].map((h) => mkTrack(h, h));
          return Promise.resolve({ status: 1, data: { list: tracks, total: tracks.length } });
        }
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockImplementation((path: string) => {
        if (path === '/playlist/tracks/add') { serverHashes.add('off-first'); return Promise.resolve({ status: 1 }); }
        return Promise.resolve({ status: 1 });
      });
      await favoriteStore.onOnline();
      expect(serverHashes.has('off-first')).toBe(true);
      expect(favoriteStore.pendingOutbox).toBe(0);
    });
  });

  describe('reconcile vs concurrent optimistic intent', () => {
    it('preserves a favorite made during the reconcile fetch (stale snapshot does not clobber)', async () => {
      let resolveTracks!: (v: unknown) => void;
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') return new Promise((r) => { resolveTracks = r; });
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockResolvedValue({ status: 1 }); // addTrackToPlaylist succeeds

      // Start onLogin; its reconcile blocks on the gated track fetch.
      const loginP = favoriteStore.onLogin('u1');
      await flushPromises();

      // During the reconcile wait, favorite X (online, succeeds).
      await favoriteStore.setFavorite(mkTrack('X', '1'), true);
      expect(favoriteStore.isFavorite('X')).toBe(true);

      // The stale snapshot (taken before the add reached the server) lacks X.
      resolveTracks({ status: 1, data: { list: [], total: 0 } });
      await loginP;
      await flushPromises();

      // X must survive the stale reconcile.
      expect(favoriteStore.isFavorite('X')).toBe(true);
    });

    it('preserves an offline (outbox) favorite made during the reconcile fetch', async () => {
      let resolveTracks!: (v: unknown) => void;
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') return new Promise((r) => { resolveTracks = r; });
        return Promise.resolve({ status: 1, data: {} });
      });

      const loginP = favoriteStore.onLogin('u1');
      await flushPromises();

      // setFavorite is offline -> enqueued to outbox (pending intent).
      mockApiPost.mockRejectedValue(new Error('down'));
      await favoriteStore.setFavorite(mkTrack('Y', '2'), true);
      expect(favoriteStore.isFavorite('Y')).toBe(true);

      // Stale snapshot lacks Y.
      resolveTracks({ status: 1, data: { list: [], total: 0 } });
      await loginP;
      await flushPromises();

      // Y must survive (outbox intent preserved over the stale snapshot).
      expect(favoriteStore.isFavorite('Y')).toBe(true);
      expect(favoriteStore.pendingOutbox).toBe(1);
    });
  });

  describe('anonymous migration write-failure safety', () => {
    it('does not clear the anonymous source when the outbox write fails', async () => {
      userStore.isLoggedIn = false;
      userStore.userId = '';
      await favoriteStore.setFavorite(mkTrack('anonQ', '1'), true);
      expect(favoriteRepository.loadAnonymousFavorites()).toHaveLength(1);

      // Simulate quota / private-mode: saveOutbox reports failure.
      vi.spyOn(favoriteRepository, 'saveOutbox').mockReturnValue(false);

      userStore.isLoggedIn = true;
      userStore.userId = 'u1';
      mockApiGet.mockImplementation((path: string) => {
        if (path === '/user/playlist') return Promise.resolve(likedPlaylistResponse('999', 'u1'));
        if (path === '/playlist/track/all') return Promise.resolve({ status: 1, data: { list: [], total: 0 } });
        return Promise.resolve({ status: 1, data: {} });
      });
      mockApiPost.mockResolvedValue({ status: 1 });
      await favoriteStore.onLogin('u1');

      // The anonymous source must still be present (write failed -> not cleared).
      expect(favoriteRepository.loadAnonymousFavorites()).toHaveLength(1);
    });
  });
});

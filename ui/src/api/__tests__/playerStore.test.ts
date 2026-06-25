import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

// Mock Tauri invoke (used by stats recording + native_request).
const mockInvoke = vi.fn();
vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: any[]) => mockInvoke(...args),
}));

// jsdom has no Web Audio; WebAudioEq's createCtx returns null so EQ is a no-op
// and the <audio> plays directly (the non-CORS degradation path).
vi.stubGlobal('AudioContext', undefined);
vi.stubGlobal('webkitAudioContext', undefined);

import {
  playerStore,
  initPlayer,
  initPlayerBackend,
  playTrack,
  playAll,
} from '../playerStore';
import type { Track } from '../normalizer';

function mkTrack(hash: string, name = hash): Track {
  return { FileHash: hash, SongName: name, SingerName: 'A', Duration: 100 } as Track;
}

/** Reset the playerStore singleton state between tests. */
function resetStore() {
  playerStore.queue = [];
  playerStore.currentIndex = -1;
  playerStore.currentTrack = null;
  playerStore.isPlaying = false;
  playerStore.currentTime = 0;
  playerStore.duration = 0;
  playerStore.errorMsg = '';
  (playerStore as any).audio = null;
}

describe('playerStore integration', () => {
  beforeEach(() => {
    localStorage.clear();
    mockInvoke.mockReset();
    resetStore();
  });
  afterEach(() => vi.useRealTimers());

  it('registers exactly one ended listener (no double-fire) and advances once', async () => {
    // #2 root cause: initPlayer registered a DIRECT 'ended' listener AND
    // initPlayerBackend's onEvent registered ANOTHER on the same <audio>.
    // One natural end fired both → next() twice → double /song/url fetch.
    // After rewire, the backend onEvent is the sole 'ended' owner.
    initPlayer();

    // Spy on addEventListener to count 'ended' registrations.
    const audio = (playerStore as any).audio as HTMLAudioElement;
    const addSpy = vi.spyOn(audio, 'addEventListener');
    initPlayerBackend();

    const endedRegs = addSpy.mock.calls.filter((c) => c[0] === 'ended');
    expect(endedRegs).toHaveLength(1); // exactly one 'ended' listener

    // Behavioral check: one ended dispatch → one /song/url fetch for the next track.
    // Tracks carry an Image so fetchCoverImage (another native_request) is skipped,
    // leaving only the /song/url fetch to count.
    const t1 = mkTrack('h1'), t2 = mkTrack('h2'), t3 = mkTrack('h3');
    [t1, t2, t3].forEach((t) => (t.Image = 'http://img/'));
    playerStore.queue = [t1, t2, t3];
    playerStore.currentIndex = 0;
    playerStore.currentTrack = t1;

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 1, url: 'http://x/' } });
    });

    // Drive the backend's 'ended' handler directly via a dispatch.
    const fetchesBefore = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
    audio.dispatchEvent(new Event('ended'));

    // The single 'ended' listener calls next() → playTrack(t2) → exactly one fetch.
    await vi.waitFor(() => {
      const fetchesAfter = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
      expect(fetchesAfter - fetchesBefore).toBe(1);
    });
    await new Promise((r) => setTimeout(r, 80));
    const fetchesAfter = mockInvoke.mock.calls.filter((c) => c[0] === 'native_request').length;
    expect(fetchesAfter - fetchesBefore).toBe(1);
  });

  it('rolls back currentIndex when /song/url fails so the failed track is not persisted as current', async () => {
    // #13: playTrack persisted currentIndex BEFORE the fetch; on failure the
    // bad track stayed as current and got re-persisted, trapping the user.
    playAll([mkTrack('good')], 0);
    const before = playerStore.currentIndex; // 0
    initPlayerBackend();

    mockInvoke.mockImplementation(async (cmd: string) => {
      if (cmd === 'stats_record_play') return '';
      return JSON.stringify({ status: 200, headers: {}, body: { status: 0, error: 'vip' } });
    });

    await playTrack(mkTrack('bad'));
    expect(playerStore.errorMsg).toBeTruthy();
    expect(playerStore.currentIndex).toBe(before);
  });

  it('does not crash on corrupt localStorage queue JSON at import time', async () => {
    // #14: bare JSON.parse(localStorage) threw at import time on corrupt data,
    // blank-screening the app. loadJSON must swallow and fall back.
    vi.resetModules();
    localStorage.clear();
    localStorage.setItem('player_queue', '{not valid json');
    localStorage.setItem('player_eq_bands', '!!!');

    // Re-importing the module triggers the module-level localStorage parse.
    const mod = await import('../playerStore');
    // Import did not throw, and state fell back to safe defaults.
    expect(mod.playerStore.queue).toEqual([]);
    expect(mod.playerStore.eqBands).toEqual([0, 0, 0, 0, 0]);
  });
});

import { describe, it, expect, beforeEach } from 'vitest';
import { createMemoryHistory } from 'vue-router';
import { createAppRouter } from '../router';
import { routeNames } from '../routes';
import { playbackDiagnostics } from '../../../playback/playbackDiagnostics';
import { initPlayer, playerStore, recordPlaybackMediaSnapshot } from '../../../playback/playerStore';
import { getMediaRuntime } from '../../../playback/runtime/mediaRuntime';

describe('navigation media snapshot (page-switch silence investigation)', () => {
  beforeEach(() => {
    playbackDiagnostics.reset();
    playerStore.currentIndex = -1;
    playerStore.queue = [];
    playerStore.currentTrack = null;
    playerStore.playbackPhase = 'idle';
  });

  it('afterEach records a media snapshot with route tag', async () => {
    initPlayer();
    const audio = getMediaRuntime()!.audio;
    audio.src = 'http://127.0.0.1:17631/audio/nav-snap';
    Object.defineProperty(audio, 'paused', { value: false, configurable: true });
    Object.defineProperty(audio, 'muted', { value: false, configurable: true });

    const router = createAppRouter(createMemoryHistory());
    await router.push({ name: routeNames.home });
    await router.push({ name: routeNames.login });

    const events = playbackDiagnostics.getEvents().filter((e) =>
      e.detail.startsWith('route:'));
    expect(events.length).toBeGreaterThan(0);
    expect(events[0]!.detail).toContain('route:');
    expect(events[0]!.detail).toMatch(/paused=|no-audio/);
  });

  it('recordPlaybackMediaSnapshot does not throw without a track', () => {
    initPlayer();
    expect(() => recordPlaybackMediaSnapshot('unit:snap')).not.toThrow();
    const ev = playbackDiagnostics.getEvents().find((e) => e.detail.startsWith('unit:snap'));
    expect(ev).toBeTruthy();
  });
});

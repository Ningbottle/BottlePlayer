import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  bindOsMediaBridge,
  unbindOsMediaBridge,
  handleOsMediaButton,
  __osMediaBridgeIsBoundForTests,
  __resetOsMediaBridgeForTests,
  type OsMediaBridgeDeps,
} from '../osMediaBridge';

describe('osMediaBridge', () => {
  beforeEach(() => {
    __resetOsMediaBridgeForTests();
  });
  afterEach(() => {
    __resetOsMediaBridgeForTests();
  });

  function mockDeps(overrides: Partial<OsMediaBridgeDeps> = {}): OsMediaBridgeDeps {
    const invoke = vi.fn().mockResolvedValue(undefined);
    const unlisten = vi.fn();
    const listen = vi.fn().mockResolvedValue(unlisten);
    return {
      invoke: invoke as unknown as OsMediaBridgeDeps['invoke'],
      listen: listen as unknown as OsMediaBridgeDeps['listen'],
      getTrack: () => ({
        title: 'Song A',
        artist: 'Artist X',
        album: 'Album',
        artworkUrl: 'http://img/a.jpg',
      }),
      getIsPlaying: () => true,
      getHasQueuePrev: () => false,
      getHasQueueNext: () => true,
      togglePlay: vi.fn(),
      next: vi.fn(),
      prev: vi.fn(),
      ...overrides,
    };
  }

  it('bind invokes os_media_bind and pushes now playing + status + controls', async () => {
    const deps = mockDeps();
    await bindOsMediaBridge(deps);
    expect(__osMediaBridgeIsBoundForTests()).toBe(true);
    expect(deps.invoke).toHaveBeenCalledWith('os_media_bind');
    expect(deps.invoke).toHaveBeenCalledWith(
      'os_media_set_now_playing',
      expect.objectContaining({
        nowPlaying: expect.objectContaining({
          title: 'Song A',
          artist: 'Artist X',
        }),
      }),
    );
    expect(deps.invoke).toHaveBeenCalledWith(
      'os_media_set_playback_status',
      expect.objectContaining({ status: 'Playing' }),
    );
    expect(deps.invoke).toHaveBeenCalledWith(
      'os_media_set_enabled_controls',
      expect.objectContaining({
        controls: expect.objectContaining({ play_pause: true, next: true, prev: false }),
      }),
    );
    expect(deps.listen).toHaveBeenCalledWith('os-media-button', expect.any(Function));
  });

  it('unbind invokes os_media_unbind', async () => {
    const deps = mockDeps();
    await bindOsMediaBridge(deps);
    await unbindOsMediaBridge(deps);
    expect(deps.invoke).toHaveBeenCalledWith('os_media_unbind');
    expect(__osMediaBridgeIsBoundForTests()).toBe(false);
  });

  it('handleOsMediaButton routes Next/Prev/PlayPause to player controls', async () => {
    const deps = mockDeps();
    await handleOsMediaButton('Next', deps);
    expect(deps.next).toHaveBeenCalled();
    await handleOsMediaButton('Prev', deps);
    expect(deps.prev).toHaveBeenCalled();
    await handleOsMediaButton('PlayPause', deps);
    expect(deps.togglePlay).toHaveBeenCalled();
  });

  it('handleOsMediaButton Play only toggles when paused; Pause only when playing', async () => {
    const playing = mockDeps({ getIsPlaying: () => true });
    await handleOsMediaButton('Play', playing);
    expect(playing.togglePlay).not.toHaveBeenCalled();
    await handleOsMediaButton('Pause', playing);
    expect(playing.togglePlay).toHaveBeenCalledTimes(1);

    const paused = mockDeps({ getIsPlaying: () => false });
    await handleOsMediaButton('Play', paused);
    expect(paused.togglePlay).toHaveBeenCalledTimes(1);
    await handleOsMediaButton('Pause', paused);
    expect(paused.togglePlay).toHaveBeenCalledTimes(1); // still 1 — Pause no-op when already paused
  });

  it('bind degrades when os_media_bind fails', async () => {
    const deps = mockDeps({
      invoke: vi.fn().mockRejectedValue(new Error('no tauri')) as unknown as OsMediaBridgeDeps['invoke'],
    });
    await bindOsMediaBridge(deps);
    expect(__osMediaBridgeIsBoundForTests()).toBe(false);
  });
});

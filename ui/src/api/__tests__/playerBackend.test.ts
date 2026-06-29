import { describe, it, expect, vi, beforeEach } from 'vitest';
import { NativePlaybackBackend } from '../nativeBackend';
import { Html5AudioBackend } from '../html5Backend';

// Mock Tauri invoke
vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { invoke } from '@tauri-apps/api/core';

describe('NativePlaybackBackend', () => {
  beforeEach(() => { vi.clearAllMocks(); });

  it('initializes with MFS first, falls back to MFP if MFS fails', async () => {
    (invoke as any)
      .mockResolvedValueOnce(false)  // MFS fails
      .mockResolvedValueOnce(true);  // MFP succeeds

    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();

    expect(ok).toBe(true);
    expect(backend.activeBackendKind).toBe('mfp');
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 1 });
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 0 });
  });

  it('initializes with MFS only when MFS succeeds', async () => {
    (invoke as any).mockResolvedValueOnce(true);
    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();
    expect(ok).toBe(true);
    expect(backend.activeBackendKind).toBe('mfs');
    expect(invoke).toHaveBeenCalledTimes(1);
  });

  it('returns false when both backends fail', async () => {
    (invoke as any).mockResolvedValue(false);
    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();
    expect(ok).toBe(false);
  });

  it('playUrl forwards to invoke with correct args', async () => {
    (invoke as any).mockResolvedValue(true);
    const backend = new NativePlaybackBackend();
    await backend.initialize();
    await backend.playUrl('https://example.com/song.mp3');
    expect(invoke).toHaveBeenCalledWith('playback_play_url', { url: 'https://example.com/song.mp3' });
  });

  it('tracks source availability across playUrl, switchUrl, stop, and shutdown', async () => {
    (invoke as any).mockResolvedValue(true);
    const backend = new NativePlaybackBackend();

    expect(backend.hasSource()).toBe(false);

    await backend.playUrl('https://example.com/one.mp3');
    expect(backend.hasSource()).toBe(true);

    await backend.switchUrl('https://example.com/two.mp3', { position: 42, autoplay: true });
    expect(invoke).toHaveBeenCalledWith('playback_play_url', { url: 'https://example.com/two.mp3' });
    expect(invoke).toHaveBeenCalledWith('playback_seek', { seconds: 42 });
    expect(backend.hasSource()).toBe(true);

    await backend.stop();
    expect(backend.hasSource()).toBe(false);

    await backend.playUrl('https://example.com/three.mp3');
    await backend.shutdown();
    expect(backend.hasSource()).toBe(false);
  });
});

describe('Html5AudioBackend', () => {
  it('playUrl triggers audio.play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);
    const ok = await backend.playUrl('https://example.com/song.mp3');
    expect(ok).toBe(true);
    expect(audio.play).toHaveBeenCalled();
    expect(audio.src).toBe('https://example.com/song.mp3');
  });

  it('playUrl uses prepared CORS-safe sources and initializes WebAudio EQ after play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    const callOrder: string[] = [];
    audio.play = vi.fn(async () => {
      callOrder.push('play');
    });
    const prepareSourceUrl = vi.fn().mockResolvedValue({
      url: 'http://127.0.0.1:17631/audio/1',
      crossOriginSafe: true,
    });
    const initEq = vi.fn(() => {
      callOrder.push('initEq');
    });
    const backend = new Html5AudioBackend(audio, { prepareSourceUrl, initEq });

    const ok = await backend.playUrl('https://cdn.example/song.mp3');

    expect(ok).toBe(true);
    expect(prepareSourceUrl).toHaveBeenCalledWith('https://cdn.example/song.mp3');
    expect(audio.crossOrigin).toBe('anonymous');
    expect(audio.src).toBe('http://127.0.0.1:17631/audio/1');
    expect(callOrder).toEqual(['play', 'initEq']);
    expect(initEq).toHaveBeenCalledWith(audio, true);
    expect(audio.play).toHaveBeenCalled();
  });

  it('playUrl does not call initEq when play() rejects', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    const playError = new Error('NotAllowedError');
    audio.play = vi.fn().mockRejectedValue(playError);
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, { initEq });

    const ok = await backend.playUrl('https://example.com/song.mp3');

    expect(ok).toBe(false);
    expect(initEq).not.toHaveBeenCalled();
  });

  it('playUrl passes crossOriginSafe=false to initEq for direct sources', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const prepareSourceUrl = vi.fn().mockResolvedValue({
      url: 'https://cdn.example/song.mp3',
      crossOriginSafe: false,
    });
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, { prepareSourceUrl, initEq });

    await backend.playUrl('https://cdn.example/song.mp3');

    expect(audio.hasAttribute('crossorigin')).toBe(false);
    expect(initEq).toHaveBeenCalledWith(audio, false);
  });

  it('stop disconnects EQ before clearing source', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.pause = vi.fn();
    audio.load = vi.fn();
    audio.src = 'https://example.com/old.mp3';
    const callOrder: string[] = [];
    const disconnectEq = vi.fn(() => callOrder.push('disconnectEq'));
    const backend = new Html5AudioBackend(audio, { disconnectEq });

    await backend.stop();

    expect(callOrder).toEqual(['disconnectEq']);
    expect(audio.pause).toHaveBeenCalled();
  });

  it('setVolume routes to EQ gain when rerouted', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    const setEqVolume = vi.fn();
    const backend = new Html5AudioBackend(audio, {
      isEqRerouted: () => true,
      setEqVolume,
    });

    await backend.setVolume(0.42);

    expect(setEqVolume).toHaveBeenCalledWith(0.42);
    expect(audio.volume).not.toBe(0.42);
  });

  it('switchUrl waits for metadata before restoring position and respects autoplay false', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);

    const switched = backend.switchUrl('https://example.com/song.mp3', {
      position: 42,
      autoplay: false,
    });
    audio.dispatchEvent(new Event('loadedmetadata'));

    await expect(switched).resolves.toBe(true);
    expect(audio.src).toBe('https://example.com/song.mp3');
    expect(audio.currentTime).toBe(42);
    expect(audio.play).not.toHaveBeenCalled();
    expect(backend.hasSource()).toBe(true);
  });

  it('stop clears source so hasSource returns false', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.pause = vi.fn();
    audio.load = vi.fn();
    audio.src = 'https://example.com/old.mp3';
    const backend = new Html5AudioBackend(audio);

    expect(backend.hasSource()).toBe(true);

    await backend.stop();

    expect(audio.pause).toHaveBeenCalled();
    expect(audio.src).toBe('');
    expect(audio.load).toHaveBeenCalled();
    expect(backend.hasSource()).toBe(false);
  });
});

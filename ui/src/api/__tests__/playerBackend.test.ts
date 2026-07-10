import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
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
  afterEach(() => { vi.restoreAllMocks(); });

  it('playUrl triggers audio.play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);
    const ok = await backend.playUrl('https://example.com/song.mp3');
    expect(ok).toBe(true);
    expect(audio.play).toHaveBeenCalled();
    expect(audio.src).toBe('https://example.com/song.mp3');
  });

  it('reports HTML5 media event diagnostics for error and stall-like events', () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    Object.defineProperty(audio, 'readyState', { value: 3, configurable: true });
    Object.defineProperty(audio, 'networkState', { value: 2, configurable: true });
    Object.defineProperty(audio, 'error', {
      value: { code: 3, message: 'decode failed' },
      configurable: true,
    });
    audio.src = 'https://example.com/song.mp3';
    audio.currentTime = 12;
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const backend = new Html5AudioBackend(audio);
    const events: Array<{ type: string; error?: string }> = [];

    const unsubscribe = backend.onEvent((event) => events.push(event));
    audio.dispatchEvent(new Event('error'));
    audio.dispatchEvent(new Event('waiting'));
    unsubscribe();

    expect(events).toEqual([
      expect.objectContaining({
        type: 'error',
        error: expect.stringContaining('HTML5 media error'),
      }),
    ]);
    expect(events[0].error).toEqual(expect.stringContaining('readyState=3'));
    expect(events[0].error).toEqual(expect.stringContaining('networkState=2'));
    expect(events[0].error).toEqual(expect.stringContaining('mediaError=3: decode failed'));
    expect(warnSpy).toHaveBeenCalledWith(
      'Html5AudioBackend media event:',
      expect.objectContaining({
        event: 'waiting',
        readyState: 3,
        networkState: 2,
        currentTime: 12,
        src: 'https://example.com/song.mp3',
        mediaError: { code: 3, message: 'decode failed' },
      }),
    );
  });

  it('records media_event diagnostics via recordDiagnostic on stall and error events', () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    Object.defineProperty(audio, 'readyState', { value: 3, configurable: true });
    Object.defineProperty(audio, 'networkState', { value: 2, configurable: true });
    Object.defineProperty(audio, 'error', {
      value: { code: 3, message: 'decode failed' },
      configurable: true,
    });
    audio.src = 'https://example.com/song.mp3';
    audio.currentTime = 12;
    vi.spyOn(console, 'warn').mockImplementation(() => {});

    const recorded: Array<{ kind: string; phase: string; detail: string }> = [];
    const backend = new Html5AudioBackend(audio, {
      recordDiagnostic: (e) => recorded.push(e as any),
    });
    const unsub = backend.onEvent(() => {});
    audio.dispatchEvent(new Event('stalled'));
    audio.dispatchEvent(new Event('error'));
    unsub();

    expect(recorded).toHaveLength(2);
    expect(recorded.every((e) => e.kind === 'media_event')).toBe(true);
    // stalled is a transient stall — phase noop; error is a failure — phase fail.
    expect(recorded[0]).toEqual(
      expect.objectContaining({ phase: 'noop', detail: expect.stringContaining('stalled') }),
    );
    expect(recorded[1]).toEqual(
      expect.objectContaining({ phase: 'fail', detail: expect.stringContaining('error') }),
    );
  });

  it('records proxy_prep diagnostics around prepareSourceUrl (ok and fail)', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn(async () => {});
    const recorded: Array<{ kind: string; phase: string; detail: string }> = [];
    const backend = new Html5AudioBackend(audio, {
      prepareSourceUrl: async (url: string) => {
        if (url.includes('fail')) throw new Error('proxy down');
        return { url: 'http://127.0.0.1:1234/proxy/song', crossOriginSafe: true };
      },
      recordDiagnostic: (e) => recorded.push(e as any),
    });

    // success path
    await backend.playUrl('https://cdn.kugou.com/song.mp3');
    expect(recorded).toContainEqual(
      expect.objectContaining({ kind: 'proxy_prep', phase: 'ok' }),
    );

    // failure path — prepareSourceUrl throws; the diagnostic is recorded
    // before the error propagates out of playUrl.
    recorded.length = 0;
    await expect(
      backend.playUrl('https://cdn.kugou.com/fail.mp3'),
    ).rejects.toThrow('proxy down');
    expect(recorded).toContainEqual(
      expect.objectContaining({ kind: 'proxy_prep', phase: 'fail' }),
    );
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

  it('playUrl skips initEq when attach transition is stale after play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, {
      getAttachTransitionSeq: () => 1,
      isAttachTransitionCurrent: () => false,
      initEq,
    });

    await backend.playUrl('https://example.com/song.mp3');

    expect(audio.play).toHaveBeenCalled();
    expect(initEq).not.toHaveBeenCalled();
  });

  it('playUrl calls initEq when attach transition is still current after play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, {
      getAttachTransitionSeq: () => 2,
      isAttachTransitionCurrent: (seq) => seq === 2,
      initEq,
    });

    await backend.playUrl('https://example.com/song.mp3');

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

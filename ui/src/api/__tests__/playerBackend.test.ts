import { describe, it, expect, vi, afterEach } from 'vitest';
import { Html5AudioBackend } from '../html5Backend';

function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((res) => { resolve = res; });
  return { promise, resolve };
}

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

  it('keeps the newest audio source when an older preparation resolves late', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    let resolveA!: (source: { url: string; crossOriginSafe: boolean }) => void;
    const prepareA = new Promise<{ url: string; crossOriginSafe: boolean }>((resolve) => {
      resolveA = resolve;
    });
    const prepareSourceUrl = vi.fn((url: string) =>
      url.endsWith('/a')
        ? prepareA
        : Promise.resolve({ url: 'http://127.0.0.1/b', crossOriginSafe: true }),
    );
    const backend = new Html5AudioBackend(audio, { prepareSourceUrl });

    const playA = backend.playUrl('https://cdn.example/a');
    const playB = backend.playUrl('https://cdn.example/b');
    await playB;

    resolveA({ url: 'http://127.0.0.1/a', crossOriginSafe: true });
    await playA;

    expect(audio.src).toContain('/b');
    expect(prepareSourceUrl).toHaveBeenCalledWith('https://cdn.example/a');
    expect(prepareSourceUrl).toHaveBeenCalledWith('https://cdn.example/b');
  });

  it('does not let preparation invalidated by stop overwrite a newer source', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    audio.pause = vi.fn();
    audio.load = vi.fn();
    let resolveA!: (source: { url: string; crossOriginSafe: boolean }) => void;
    const prepareA = new Promise<{ url: string; crossOriginSafe: boolean }>((resolve) => {
      resolveA = resolve;
    });
    const backend = new Html5AudioBackend(audio, {
      prepareSourceUrl: (url) => url.endsWith('/a')
        ? prepareA
        : Promise.resolve({ url: 'http://127.0.0.1/b', crossOriginSafe: true }),
    });

    const playA = backend.playUrl('https://cdn.example/a');
    await backend.stop();
    await backend.playUrl('https://cdn.example/b');

    resolveA({ url: 'http://127.0.0.1/a', crossOriginSafe: true });
    await expect(playA).resolves.toBe(false);
    expect(audio.src).toContain('/b');
    expect(audio.pause).toHaveBeenCalledTimes(1);
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
    expect(initEq).toHaveBeenCalledWith(audio, true, expect.any(Function));
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
    expect(initEq).toHaveBeenCalledWith(audio, false, expect.any(Function));
  });

  it('playUrl abandons a stale entry transition before it can play or attach EQ', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, {
      getAttachTransitionSeq: () => 1,
      isAttachTransitionCurrent: () => false,
      initEq,
    });

    await backend.playUrl('https://example.com/song.mp3');

    expect(audio.play).not.toHaveBeenCalled();
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

    expect(initEq).toHaveBeenCalledWith(audio, false, expect.any(Function));
  });

  it('does not let a stale post-play async EQ attach reroute the newer source', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const attachMayFinish = deferred<void>();
    let attachStarted!: () => void;
    const attachStartedPromise = new Promise<void>((resolve) => { attachStarted = resolve; });
    const attachCount = { a: 0, b: 0 };
    let attachAttempt = 0;
    const initEq = vi.fn(async (...args: unknown[]) => {
      const isCurrent = args[2] as (() => boolean) | undefined;
      const intent = attachAttempt++ === 0 ? 'a' : 'b';
      attachStarted();
      await attachMayFinish.promise;
      if (isCurrent?.()) attachCount[intent] += 1;
    });
    const backend = new Html5AudioBackend(audio, { initEq: initEq as any });

    const playA = backend.playUrl('https://example.com/a.mp3');
    await attachStartedPromise;
    const playB = backend.playUrl('https://example.com/b.mp3');
    attachMayFinish.resolve();
    await playB;
    await playA;

    expect(initEq).toHaveBeenCalledTimes(2);
    expect(attachCount.a).toBe(0);
    expect(attachCount.b).toBe(1);
  });

  it('playUrl disconnects EQ before assigning a new src', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    audio.src = 'https://example.com/old.mp3';
    const callOrder: string[] = [];
    Object.defineProperty(audio, 'src', {
      configurable: true,
      get: () => (audio.getAttribute('src') || ''),
      set: (v: string) => {
        callOrder.push(`src=${v}`);
        audio.setAttribute('src', v);
      },
    });
    const disconnectEq = vi.fn(() => callOrder.push('disconnectEq'));
    const backend = new Html5AudioBackend(audio, { disconnectEq });

    await backend.playUrl('https://example.com/new.mp3');

    expect(callOrder[0]).toBe('disconnectEq');
    expect(callOrder.some((step) => step.startsWith('src='))).toBe(true);
    expect(callOrder.indexOf('disconnectEq')).toBeLessThan(
      callOrder.findIndex((step) => step.startsWith('src=')),
    );
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

  // ── B4: the Backend owns media volume only — persistence belongs to playerPersistence ──

  it('does not read or write persisted volume', async () => {
    // Seed a conflicting preference directly (before the setItem spy so the
    // seed write itself is not counted).
    localStorage.setItem('player_volume', '0.9');
    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');

    const audio = document.createElement('audio') as HTMLAudioElement;
    const backend = new Html5AudioBackend(audio, { initialVolume: 0.25 });

    // Constructor must use options.initialVolume, not the stored 0.9.
    expect(audio.volume).toBe(0.25);

    await backend.setVolume(0.4);
    expect(audio.volume).toBe(0.4);

    const persistedWrites = setItemSpy.mock.calls.filter(
      ([key]) => key === 'player_volume',
    );
    expect(
      persistedWrites,
      'Backend must never write player_volume (persistence is single-owner)',
    ).toHaveLength(0);
    setItemSpy.mockRestore();
    localStorage.removeItem('player_volume');
  });

  it('defaults to 0.7 when no initialVolume option is provided', () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    new Html5AudioBackend(audio);
    expect(audio.volume).toBe(0.7);
  });

  it.each([
    [Number.NaN, 0.7],
    [Number.POSITIVE_INFINITY, 0.7],
    [1.5, 1],
    [-0.25, 0],
  ] as const)(
    'clamps or falls back for out-of-range initialVolume %s → %s',
    (raw, expected) => {
      const audio = document.createElement('audio') as HTMLAudioElement;
      new Html5AudioBackend(audio, { initialVolume: raw });
      // HTMLMediaElement.volume must never receive an out-of-range value.
      expect(audio.volume).toBe(expected);
    },
  );

  it('switchUrl waits for metadata before restoring position and respects autoplay false', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);

    const switched = backend.switchUrl('https://example.com/song.mp3', {
      position: 42,
      autoplay: false,
    });
    const addListener = vi.spyOn(audio, 'addEventListener');
    await vi.waitFor(() => expect(addListener).toHaveBeenCalledWith(
      'loadedmetadata', expect.any(Function), { once: true },
    ));
    audio.dispatchEvent(new Event('loadedmetadata'));

    await expect(switched).resolves.toBe(true);
    expect(audio.src).toBe('https://example.com/song.mp3');
    expect(audio.currentTime).toBe(42);
    expect(audio.play).not.toHaveBeenCalled();
    expect(backend.hasSource()).toBe(true);
  });

  it('does not attach EQ when a pending audio.play loses its source lease', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    const finishA = deferred<void>();
    audio.play = vi.fn()
      .mockImplementationOnce(() => finishA.promise)
      .mockResolvedValueOnce(undefined);
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, { initEq });

    const playA = backend.playUrl('https://example.com/a.mp3');
    await vi.waitFor(() => expect(audio.play).toHaveBeenCalledTimes(1));
    await backend.playUrl('https://example.com/b.mp3');
    finishA.resolve();

    await expect(playA).resolves.toBe(false);
    expect(audio.src).toBe('https://example.com/b.mp3');
    expect(initEq).toHaveBeenCalledTimes(1);
  });

  it.each([
    ['playUrl', (backend: Html5AudioBackend) => backend.playUrl('https://example.com/a.mp3')],
    ['switchUrl', (backend: Html5AudioBackend) => backend.switchUrl('https://example.com/a.mp3', { position: 12, autoplay: true })],
  ])('does not write a prepared %s source after an epoch-only supersession', async (_name, begin) => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    audio.src = 'https://example.com/b.mp3';
    audio.crossOrigin = 'anonymous';
    const preparedA = deferred<{ url: string; crossOriginSafe: boolean }>();
    let epoch = 1;
    const initEq = vi.fn();
    const backend = new Html5AudioBackend(audio, {
      prepareSourceUrl: () => preparedA.promise,
      getAttachTransitionSeq: () => epoch,
      isAttachTransitionCurrent: (seq) => seq === epoch,
      initEq,
    });

    const a = begin(backend);
    epoch = 2; // B resumed/replayed; no new backend lease was created.
    preparedA.resolve({ url: 'https://example.com/a.mp3', crossOriginSafe: false });

    await expect(a).resolves.toBe(false);
    expect(audio.src).toBe('https://example.com/b.mp3');
    expect(audio.crossOrigin).toBe('anonymous');
    expect(audio.play).not.toHaveBeenCalled();
    expect(initEq).not.toHaveBeenCalled();
  });

  it('does not seek or play an older switchUrl after a newer source takes ownership', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);

    const switchA = backend.switchUrl('https://example.com/a.mp3', {
      position: 42,
      autoplay: true,
    });
    await backend.switchUrl('https://example.com/b.mp3', { autoplay: false });
    audio.dispatchEvent(new Event('loadedmetadata'));

    await expect(switchA).resolves.toBe(false);
    expect(audio.src).toBe('https://example.com/b.mp3');
    expect(audio.currentTime).toBe(0);
    expect(audio.play).not.toHaveBeenCalled();
  });

  it('abandons a metadata wait when its captured transition epoch is superseded without a new lease', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    const addListener = vi.spyOn(audio, 'addEventListener');
    let epoch = 1;
    const backend = new Html5AudioBackend(audio, {
      getAttachTransitionSeq: () => epoch,
      isAttachTransitionCurrent: (seq) => seq === epoch,
    });

    const switchA = backend.switchUrl('https://example.com/a.mp3', {
      position: 42,
      autoplay: false,
    });
    await vi.waitFor(() => expect(addListener).toHaveBeenCalledWith(
      'loadedmetadata', expect.any(Function), { once: true },
    ));
    epoch = 2; // B resumed/replayed: orchestrator epoch changed, source lease did not.
    audio.dispatchEvent(new Event('loadedmetadata'));

    await expect(switchA).resolves.toBe(false);
    expect(audio.currentTime).toBe(0);
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

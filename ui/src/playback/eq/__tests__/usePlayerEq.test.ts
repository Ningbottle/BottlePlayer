import { describe, it, expect, vi, beforeEach } from 'vitest';

const attachSource = vi.fn();
const disconnectSource = vi.fn();
const resume = vi.fn();
const awaitReady = vi.fn();
const releaseLease = vi.fn();
const enterDegradation = vi.fn();
const recoverFromDegradation = vi.fn();
const setVolume = vi.fn();
const setEnabled = vi.fn();
const setBand = vi.fn();
const init = vi.fn();
const close = vi.fn();

let rerouted = false;
let leaseId = 0;
let contextState = 'running';
let graphReady = true;

vi.mock('../webAudioEq', () => ({
  WebAudioEq: class {
    init = init;
    attachSource = (...args: unknown[]) => attachSource(...args);
    disconnectSource = disconnectSource;
    resume = (...args: unknown[]) => resume(...args);
    awaitReady = (...args: unknown[]) => awaitReady(...args);
    releaseLease = (...args: unknown[]) => releaseLease(...args);
    enterDegradation = enterDegradation;
    recoverFromDegradation = recoverFromDegradation;
    setVolume = setVolume;
    setEnabled = setEnabled;
    setBand = setBand;
    close = close;
    get isRerouted() { return rerouted; }
    get currentLeaseId() { return leaseId; }
    get contextState() { return contextState; }
    get isGraphReady() { return graphReady; }
  },
}));

vi.mock('../../../platform/tauri/audioProxy', () => ({
  prepareAudioSourceUrl: vi.fn(async (url: string) => ({ url, crossOriginSafe: true })),
}));

import { createPlayerEq, type PlayerEqDeps } from '../usePlayerEq';
import { prepareAudioSourceUrl } from '../../../platform/tauri/audioProxy';

function makeAudio(src = 'http://127.0.0.1:9/audio/x'): HTMLAudioElement {
  return {
    volume: 0.7,
    src,
    currentSrc: src,
    getAttribute: (name: string) => (name === 'src' ? src : null),
  } as unknown as HTMLAudioElement;
}

describe('createPlayerEq attach lease', () => {
  let eqDeps: PlayerEqDeps;
  let eq: ReturnType<typeof createPlayerEq>;

  beforeEach(() => {
    vi.clearAllMocks();
    rerouted = false;
    leaseId = 0;
    contextState = 'running';
    graphReady = true;
    awaitReady.mockResolvedValue(undefined);
    resume.mockResolvedValue(undefined);
    recoverFromDegradation.mockReturnValue(true);
    attachSource.mockImplementation((_audio: HTMLAudioElement, _vol: number) => {
      leaseId += 1;
      rerouted = true;
      return true;
    });
    eqDeps = {
      getAudio: () => null,
      getVolume: () => 0.7,
      getEqEnabled: () => true,
      getEqBands: () => [0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    };
    eq = createPlayerEq(eqDeps);
  });

  it('CORS-unsafe sources restore store.volume and do not attach', async () => {
    const audio = makeAudio('https://cdn.example/song.mp3');
    audio.volume = 0;
    await eq.attachWebAudioEqSource(audio, false);
    expect(attachSource).not.toHaveBeenCalled();
    expect(audio.volume).toBe(0.7);
    expect(eq.eqState.available).toBe(false);
  });

  it('resume reject restores store.volume and skips attach', async () => {
    resume.mockRejectedValue(new Error('NotAllowedError'));
    const audio = makeAudio();
    audio.volume = 0;
    await eq.attachWebAudioEqSource(audio, true);
    expect(attachSource).not.toHaveBeenCalled();
    expect(audio.volume).toBe(0.7);
    expect(eq.eqState.available).toBe(false);
    expect(eq.eqState.reason).toContain('EQ 暂不可用');
  });

  it('AudioContext still suspended after resume restores volume and skips attach', async () => {
    resume.mockResolvedValue(undefined);
    contextState = 'suspended';
    const audio = makeAudio();
    audio.volume = 0;
    await eq.attachWebAudioEqSource(audio, true);
    expect(attachSource).not.toHaveBeenCalled();
    expect(audio.volume).toBe(0.7);
    expect(eq.eqState.available).toBe(false);
  });

  it('attachSource false restores store.volume and marks degraded', async () => {
    attachSource.mockReturnValue(false);
    rerouted = false;
    const audio = makeAudio();
    audio.volume = 0;
    await eq.attachWebAudioEqSource(audio, true);
    expect(audio.volume).toBe(0.7);
    expect(eq.eqState.available).toBe(false);
  });

  it('stale isCurrent after a successful attach releases only that lease', async () => {
    let current = true;
    const audio = makeAudio();
    attachSource.mockImplementation(() => {
      current = false;
      leaseId = 7;
      rerouted = true;
      return true;
    });
    await eq.attachWebAudioEqSource(audio, true, () => current);
    expect(releaseLease).toHaveBeenCalledWith(7);
    expect(setVolume).not.toHaveBeenCalled();
  });

  it('successful current attach syncs gain volume', async () => {
    const audio = makeAudio();
    await eq.attachWebAudioEqSource(audio, true);
    expect(attachSource).toHaveBeenCalledWith(audio, 0.7);
    expect(eq.eqState.available).toBe(true);
    expect(setVolume).toHaveBeenCalledWith(0.7);
  });

  it('defers WebAudio graph construction until an enabled safe playback source is prepared', async () => {
    eq.initWebAudioEQ();
    expect(init).not.toHaveBeenCalled();

    const prepared = await eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/track.mp3');

    expect(prepared).toEqual({ url: 'https://song.example/track.mp3', crossOriginSafe: true });
    expect(init).toHaveBeenCalledTimes(1);
    expect(awaitReady).toHaveBeenCalledTimes(1);
  });

  it('does not construct the WebAudio graph for unsafe or EQ-disabled sources', async () => {
    eq.initWebAudioEQ();
    vi.mocked(prepareAudioSourceUrl).mockResolvedValueOnce({
      url: 'https://cdn.example/direct.mp3',
      crossOriginSafe: false,
    });

    await eq.makeBackendEqHooks().prepareSourceUrl('https://cdn.example/direct.mp3');
    expect(init).not.toHaveBeenCalled();

    eqDeps.getEqEnabled = () => false;
    await eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/disabled.mp3');
    expect(prepareAudioSourceUrl).toHaveBeenCalledTimes(1);
    expect(init).not.toHaveBeenCalled();
  });

  it('does not resume or mark EQ degraded before the graph has been requested', async () => {
    eq.initWebAudioEQ();
    eqDeps.getAudio = () => makeAudio();
    contextState = 'closed';

    eq.resumeAudioContext();
    await Promise.resolve();

    expect(resume).not.toHaveBeenCalled();
    expect(eq.eqState.reason).toBe('当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。');
  });

  it('keeps source preparation usable when optional EQ graph creation rejects, then retries', async () => {
    graphReady = false;
    awaitReady.mockRejectedValueOnce(new Error('AudioContext constructor failed'));

    await expect(
      eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/first.mp3'),
    ).resolves.toEqual({ url: 'https://song.example/first.mp3', crossOriginSafe: true });
    expect(eq.eqState.reason).toContain('EQ 暂不可用');
    expect(init).toHaveBeenCalledTimes(1);

    graphReady = true;
    awaitReady.mockResolvedValueOnce(undefined);
    await expect(
      eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/second.mp3'),
    ).resolves.toEqual({ url: 'https://song.example/second.mp3', crossOriginSafe: true });
    expect(init).toHaveBeenCalledTimes(2);
  });

  it('does not let an old graph wait restart EQ after close', async () => {
    let resolveReady!: () => void;
    awaitReady.mockImplementationOnce(() => new Promise<void>((resolve) => {
      resolveReady = resolve;
    }));
    let bands = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    eqDeps.getEqBands = () => bands;

    const preparing = eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/pending.mp3');
    await Promise.resolve();
    bands = [1, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    eq.closeWebAudioEq();
    resolveReady();

    await expect(preparing).resolves.toEqual({
      url: 'https://song.example/pending.mp3',
      crossOriginSafe: true,
    });
    expect(init).toHaveBeenCalledTimes(1);
  });

  it('does not start a graph when source preparation finishes after close', async () => {
    let resolvePreparation!: (value: { url: string; crossOriginSafe: boolean }) => void;
    vi.mocked(prepareAudioSourceUrl).mockImplementationOnce(() => new Promise((resolve) => {
      resolvePreparation = resolve;
    }));

    const preparing = eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/slow.mp3');
    await Promise.resolve();
    eq.closeWebAudioEq();
    resolvePreparation({ url: 'http://127.0.0.1:9/audio/slow', crossOriginSafe: true });

    await expect(preparing).resolves.toEqual({
      url: 'http://127.0.0.1:9/audio/slow',
      crossOriginSafe: true,
    });
    expect(init).not.toHaveBeenCalled();
  });

  it('invalidates an attach that is waiting when EQ is disabled', async () => {
    let resolveReady!: () => void;
    awaitReady.mockImplementationOnce(() => new Promise<void>((resolve) => {
      resolveReady = resolve;
    }));
    const audio = makeAudio();
    audio.volume = 0;
    eqDeps.getAudio = () => audio;

    const attaching = eq.attachWebAudioEqSource(audio, true);
    await Promise.resolve();
    eq.setWebAudioEqEnabled(false);
    resolveReady();
    await attaching;

    expect(attachSource).not.toHaveBeenCalled();
    expect(audio.volume).toBe(0.7);
  });

  it('rebuilds a failed graph when the user retries EQ', async () => {
    const audio = makeAudio();
    eqDeps.getAudio = () => audio;
    graphReady = false;

    await eq.retryEq();
    expect(init).toHaveBeenCalledTimes(1);
    expect(eq.eqState.retryFailCount).toBe(1);

    graphReady = true;
    await eq.retryEq();
    expect(init).toHaveBeenCalledTimes(2);
    expect(recoverFromDegradation).toHaveBeenCalledWith(audio, 0.7);
    expect(eq.eqState.retryFailCount).toBe(0);
  });

  it('coalesces concurrent EQ retries into one graph/resume/recovery attempt', async () => {
    const audio = makeAudio();
    eqDeps.getAudio = () => audio;
    let resolveResume!: () => void;
    resume.mockImplementationOnce(() => new Promise<void>((resolve) => {
      resolveResume = resolve;
    }));

    const first = eq.retryEq();
    const second = eq.retryEq();
    expect(second).toBe(first);
    await Promise.resolve();
    await Promise.resolve();
    resolveResume();
    await Promise.all([first, second]);

    expect(init).toHaveBeenCalledTimes(1);
    expect(resume).toHaveBeenCalledTimes(1);
    expect(recoverFromDegradation).toHaveBeenCalledTimes(1);
  });

  it('does not let an old retry recover or penalize a replacement source', async () => {
    const firstAudio = makeAudio('http://127.0.0.1:9/audio/first');
    const secondAudio = makeAudio('https://cdn.example/direct.mp3');
    let currentAudio = firstAudio;
    eqDeps.getAudio = () => currentAudio;
    let resolveResume!: () => void;
    resume.mockImplementationOnce(() => new Promise<void>((resolve) => {
      resolveResume = resolve;
    }));

    const retrying = eq.retryEq();
    await Promise.resolve();
    await Promise.resolve();
    currentAudio = secondAudio;
    eq.disconnectWebAudioEqSource();
    resolveResume();
    await retrying;

    expect(recoverFromDegradation).not.toHaveBeenCalled();
    expect(eq.eqState.retryFailCount).toBe(0);
    expect(secondAudio.volume).toBe(0.7);
  });

  it('ignores an old resume rejection after the active source changes', async () => {
    const firstAudio = makeAudio('http://127.0.0.1:9/audio/first');
    const secondAudio = makeAudio('http://127.0.0.1:9/audio/second');
    let currentAudio = firstAudio;
    eqDeps.getAudio = () => currentAudio;
    await eq.makeBackendEqHooks().prepareSourceUrl('https://song.example/first.mp3');
    rerouted = true;
    let rejectResume!: (reason: Error) => void;
    resume.mockImplementationOnce(() => new Promise<void>((_resolve, reject) => {
      rejectResume = reject;
    }));

    eq.resumeAudioContext();
    currentAudio = secondAudio;
    eq.disconnectWebAudioEqSource();
    rejectResume(new Error('late resume rejection'));
    await Promise.resolve();
    await Promise.resolve();

    expect(enterDegradation).not.toHaveBeenCalled();
    expect(secondAudio.volume).toBe(0.7);
  });
});

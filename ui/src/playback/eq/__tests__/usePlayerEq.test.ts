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
  },
}));

vi.mock('../../../platform/tauri/audioProxy', () => ({
  prepareAudioSourceUrl: vi.fn(async (url: string) => ({ url, crossOriginSafe: true })),
}));

import { createPlayerEq, type PlayerEqDeps } from '../usePlayerEq';

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
    awaitReady.mockResolvedValue(undefined);
    resume.mockResolvedValue(undefined);
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
});

import { describe, it, expect, vi } from 'vitest';
import { computeRms, createAudioLevelMonitor } from '../audioLevelMonitor';

vi.mock('../motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

describe('computeRms', () => {
  it('returns 0 for silence (all samples at 128)', () => {
    expect(computeRms(new Uint8Array([128, 128, 128, 128]))).toBe(0);
  });

  it('returns 0 for empty input', () => {
    expect(computeRms(new Uint8Array(0))).toBe(0);
  });

  it('approaches 1 for full-scale square wave', () => {
    const samples = new Uint8Array(64);
    for (let i = 0; i < 64; i++) samples[i] = i % 2 === 0 ? 255 : 0;
    const rms = computeRms(samples);
    expect(rms).toBeGreaterThan(0.9);
    expect(rms).toBeLessThanOrEqual(1);
  });

  it('is bounded at 1', () => {
    const samples = new Uint8Array(64).fill(255);
    expect(computeRms(samples)).toBeLessThanOrEqual(1);
  });
});

describe('createAudioLevelMonitor fallback', () => {
  it('stays inert when captureStream and AudioContext are unavailable', () => {
    const audio = document.createElement('audio');
    const monitor = createAudioLevelMonitor(audio);

    expect(() => monitor.start()).not.toThrow();
    expect(monitor.level.value).toBe(0);
    expect(monitor.getAnalyser()).toBeNull();
    expect(() => monitor.stop()).not.toThrow();
    expect(monitor.level.value).toBe(0);
  });

  it('start is idempotent', () => {
    const audio = document.createElement('audio');
    const monitor = createAudioLevelMonitor(audio);

    monitor.start();
    monitor.start();
    expect(monitor.level.value).toBe(0);
    monitor.stop();
  });
});

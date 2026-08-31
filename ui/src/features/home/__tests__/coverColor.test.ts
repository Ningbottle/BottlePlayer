import { describe, it, expect } from 'vitest';
import { averagePixels, extractDominantColor } from '../coverColor';

describe('averagePixels', () => {
  it('returns null for empty input', () => {
    expect(averagePixels(new Uint8ClampedArray(0))).toBeNull();
  });

  it('averages a uniform field to that exact color', () => {
    const data = new Uint8ClampedArray(4 * 4);
    for (let i = 0; i < data.length; i += 4) {
      data[i] = 62;
      data[i + 1] = 214;
      data[i + 2] = 162;
      data[i + 3] = 255;
    }
    expect(averagePixels(data)).toEqual([62, 214, 162]);
  });

  it('weights saturated pixels over gray ones', () => {
    // 3 neutral grays + 1 saturated red — result must lean red, not gray.
    const data = new Uint8ClampedArray([
      128, 128, 128, 255,
      128, 128, 128, 255,
      128, 128, 128, 255,
      255, 0, 0, 255,
    ]);
    const [r, g, b] = averagePixels(data)!;
    expect(r).toBeGreaterThan(180);
    expect(g).toBeLessThan(100);
    expect(b).toBeLessThan(100);
  });
});

describe('extractDominantColor', () => {
  it('resolves null for an empty url without touching the network', async () => {
    await expect(extractDominantColor('')).resolves.toBeNull();
  });
});

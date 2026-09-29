import { afterEach, describe, it, expect, vi } from 'vitest';
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

  it('opts the tiny cover sampling canvas into frequent readback and preserves its tint', async () => {
    const pixels = new Uint8ClampedArray([42, 120, 180, 255]);
    const context = {
      drawImage: vi.fn(),
      getImageData: vi.fn(() => ({ data: pixels })),
    } as unknown as CanvasRenderingContext2D;
    const canvas = {
      width: 0,
      height: 0,
      getContext: vi.fn(() => context),
    } as unknown as HTMLCanvasElement;
    const createElement = vi.spyOn(document, 'createElement').mockImplementation(((tagName: string) => {
      if (tagName === 'canvas') return canvas;
      return document.createElementNS('http://www.w3.org/1999/xhtml', tagName) as HTMLElement;
    }) as typeof document.createElement);

    class LoadedImage {
      crossOrigin: string | null = null;
      onload: (() => void) | null = null;
      onerror: (() => void) | null = null;
      set src(_value: string) {
        queueMicrotask(() => this.onload?.());
      }
    }
    vi.stubGlobal('Image', LoadedImage);

    try {
      await expect(extractDominantColor('data:image/svg+xml,cover-readback-fixture'))
        .resolves.toEqual([42, 120, 180]);
      expect(canvas.width).toBe(32);
      expect(canvas.height).toBe(32);
      expect(canvas.getContext).toHaveBeenCalledWith('2d', { willReadFrequently: true });
      expect(context.drawImage).toHaveBeenCalledTimes(1);
      expect(context.getImageData).toHaveBeenCalledWith(0, 0, 32, 32);
    } finally {
      createElement.mockRestore();
      vi.unstubAllGlobals();
    }
  });
});

afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});

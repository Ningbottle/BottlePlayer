/**
 * coverColor.ts — 从封面提取主色，驱动氛围着色（不改用户 accent）。
 *
 * Canvas 降采样 + 饱和度加权平均：灰调/米色封面不会把氛围洗白。
 * 跨域污染、加载失败、无 Canvas 环境 → 静默回退 null（调用方用默认色）。
 */

export type RGB = [number, number, number];

const cache = new Map<string, Promise<RGB | null>>();

export function extractDominantColor(url: string): Promise<RGB | null> {
  if (!url) return Promise.resolve(null);
  let pending = cache.get(url);
  if (!pending) {
    pending = sample(url).catch(() => null);
    cache.set(url, pending);
  }
  return pending;
}

/** Pure: saturation-weighted average of RGBA pixels. Exported for tests. */
export function averagePixels(data: Uint8ClampedArray): RGB | null {
  let r = 0;
  let g = 0;
  let b = 0;
  let w = 0;
  for (let i = 0; i + 3 < data.length; i += 4) {
    const pr = data[i];
    const pg = data[i + 1];
    const pb = data[i + 2];
    const max = Math.max(pr, pg, pb);
    const min = Math.min(pr, pg, pb);
    const sat = max === 0 ? 0 : (max - min) / max;
    const lum = (0.2126 * pr + 0.7152 * pg + 0.0722 * pb) / 255;
    const weight = 0.25 + sat * 1.5 - Math.abs(lum - 0.5) * 0.4;
    if (weight <= 0) continue;
    r += pr * weight;
    g += pg * weight;
    b += pb * weight;
    w += weight;
  }
  if (w === 0) return null;
  return [Math.round(r / w), Math.round(g / w), Math.round(b / w)];
}

async function sample(url: string): Promise<RGB | null> {
  const img = new Image();
  img.crossOrigin = 'anonymous';
  await new Promise<void>((resolve, reject) => {
    img.onload = () => resolve();
    img.onerror = () => reject(new Error('cover load failed'));
    img.src = url;
  });
  const SIZE = 32;
  const canvas = document.createElement('canvas');
  canvas.width = SIZE;
  canvas.height = SIZE;
  const ctx = canvas.getContext('2d');
  if (!ctx) return null;
  ctx.drawImage(img, 0, 0, SIZE, SIZE);
  // getImageData throws on tainted canvas — caught by extractDominantColor
  return averagePixels(ctx.getImageData(0, 0, SIZE, SIZE).data);
}

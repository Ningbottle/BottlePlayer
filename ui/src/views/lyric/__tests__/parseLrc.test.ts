import { describe, it, expect } from 'vitest';
import { parseLrc } from '../useLyricStage';

describe('parseLrc', () => {
  it('parses standard timed lines in order', () => {
    const lines = parseLrc(
      '[00:00.00]First\n[00:05.50]Second\n[01:00.00]Third',
    );
    expect(lines.map((l) => l.text)).toEqual(['First', 'Second', 'Third']);
    expect(lines[0].time).toBe(0);
    expect(lines[1].time).toBeCloseTo(5.5, 2);
    expect(lines[2].time).toBe(60);
  });

  it('keeps the first half of a long lyric list (does not drop early lines)', () => {
    const body = Array.from({ length: 20 }, (_, i) => {
      const m = String(Math.floor(i / 2)).padStart(2, '0');
      const s = String((i % 2) * 30).padStart(2, '0');
      return `[${m}:${s}.00]Line ${i + 1}`;
    }).join('\n');
    const lines = parseLrc(body);
    expect(lines).toHaveLength(20);
    expect(lines[0].text).toBe('Line 1');
    expect(lines[9].text).toBe('Line 10');
    expect(lines[19].text).toBe('Line 20');
  });

  it('strips enhanced word tags and accepts colon fraction', () => {
    const lines = parseLrc('[00:01:20]Hello <00:01:40>world');
    expect(lines).toHaveLength(1);
    expect(lines[0].text).toBe('Hello world');
    expect(lines[0].time).toBeCloseTo(1.2, 2);
  });

  it('decodes base64 LRC when raw has no timestamps', () => {
    // Latin-only payload: btoa is Latin1 in jsdom; real API uses UTF-8 base64.
    const raw = '[00:00.00]Hello\n[00:02.00]World';
    const b64 = btoa(raw);
    const lines = parseLrc(b64);
    expect(lines.map((l) => l.text)).toEqual(['Hello', 'World']);
  });
});

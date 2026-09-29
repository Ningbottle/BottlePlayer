import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

const auroraSource = readFileSync(resolve(__dirname, '../AuroraLyricStage.vue'), 'utf8');
const lyricsCss = readFileSync(resolve(__dirname, '../lyrics.css'), 'utf8');

function styleBlock(source: string, selector: string): string {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const match = source.match(new RegExp(`${escaped}\\s*\\{([\\s\\S]*?)\\n\\}`));
  if (!match) {
    throw new Error(`missing style block for ${selector}`);
  }
  return match[1];
}

describe('Aurora karaoke fill geometry', () => {
  it('does not hardcode a different left padding on the highlight layer', () => {
    const fill = styleBlock(auroraSource, '.lyric-line-fill');
    expect(fill).toMatch(/padding-left:\s*inherit/);
    expect(fill).not.toMatch(/padding-left:\s*1\.5em/);
  });

  it('keeps the global left-align padding on the line, not as a second geometry for the fill', () => {
    expect(lyricsCss).toContain('html.lyric-left .lyric-line');
    const leftLine = styleBlock(lyricsCss, 'html.lyric-left .lyric-line');
    expect(leftLine).toContain('padding-left: 24px');
    expect(lyricsCss).not.toMatch(/html\.lyric-left\s+\.lyric-line-fill\s*\{[^}]*padding-left:\s*1\.5em/);
  });
});

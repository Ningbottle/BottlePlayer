import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const uiRoot = resolve(__dirname, '..', '..', '..', '..');

describe('Aurora font assets', () => {
  it('self-hosts Noto Serif SC (no Google Fonts CDN dependency)', () => {
    const html = readFileSync(resolve(uiRoot, 'index.html'), 'utf8');
    // Must not depend on the Google Fonts CDN (blocked offline / in CN ->
    // blurry substitution on machines without the font).
    expect(html).not.toMatch(/fonts\.googleapis\.com/);
    expect(html).not.toMatch(/family=Noto\+Serif\+SC/);
    // Fonts are bundled via @fontsource (imported in main.ts).
    const main = readFileSync(resolve(uiRoot, 'src', 'main.ts'), 'utf8');
    expect(main).toMatch(/@fontsource\/noto-serif-sc/);
  });

  it('Aurora --font-sans uses Noto Serif SC (serif), not SF Pro / MiSans', () => {
    const css = readFileSync(resolve(uiRoot, 'src', 'style.css'), 'utf8');
    const auroraBlock = css.slice(css.indexOf('[data-skin="aurora"]'));
    const sansDecl = auroraBlock.slice(
      auroraBlock.indexOf('--font-sans'),
      auroraBlock.indexOf(';', auroraBlock.indexOf('--font-sans')),
    );
    expect(sansDecl).toContain('Noto Serif SC');
    expect(sansDecl).not.toContain('SF Pro');
    expect(sansDecl).not.toContain('MiSans');
  });
});

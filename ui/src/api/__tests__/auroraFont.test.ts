import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const uiRoot = resolve(__dirname, '..', '..', '..');

describe('Aurora font assets', () => {
  it('index.html loads Noto Serif SC as a web font', () => {
    const html = readFileSync(resolve(uiRoot, 'index.html'), 'utf8');
    expect(html).toMatch(/family=Noto\+Serif\+SC/);
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

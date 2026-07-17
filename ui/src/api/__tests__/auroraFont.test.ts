import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const uiRoot = resolve(__dirname, '..', '..', '..');

describe('Aurora font assets', () => {
  it('index.html loads MiSans as a web font', () => {
    const html = readFileSync(resolve(uiRoot, 'index.html'), 'utf8');
    expect(html).toMatch(/misans\/lib\/Normal\/MiSans-Normal/);
  });

  it('Aurora --font-sans uses MiSans, not SF Pro', () => {
    const css = readFileSync(resolve(uiRoot, 'src', 'style.css'), 'utf8');
    const auroraBlock = css.slice(css.indexOf('[data-skin="aurora"]'));
    const sansDecl = auroraBlock.slice(
      auroraBlock.indexOf('--font-sans'),
      auroraBlock.indexOf(';', auroraBlock.indexOf('--font-sans')),
    );
    expect(sansDecl).toContain('MiSans');
    expect(sansDecl).not.toContain('SF Pro');
  });
});

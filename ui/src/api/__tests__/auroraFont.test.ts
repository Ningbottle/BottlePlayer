import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const uiRoot = resolve(__dirname, '..', '..', '..');

describe('Aurora font assets', () => {
  it('index.html loads Inter and Noto Sans SC as web fonts', () => {
    const html = readFileSync(resolve(uiRoot, 'index.html'), 'utf8');
    expect(html).toMatch(/family=Inter/);
    expect(html).toMatch(/Noto\+Sans\+SC/);
  });

  it('Aurora --font-sans uses Inter, not SF Pro', () => {
    const css = readFileSync(resolve(uiRoot, 'src', 'style.css'), 'utf8');
    const auroraBlock = css.slice(css.indexOf('[data-skin="aurora"]'));
    const sansDecl = auroraBlock.slice(
      auroraBlock.indexOf('--font-sans'),
      auroraBlock.indexOf(';', auroraBlock.indexOf('--font-sans')),
    );
    expect(sansDecl).toContain('Inter');
    expect(sansDecl).not.toContain('SF Pro');
  });
});

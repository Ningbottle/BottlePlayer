import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

const uiRoot = resolve(__dirname, '../../..');
const globalCss = readFileSync(resolve(uiRoot, 'src/styles/base.css'), 'utf8');
const skinCss = [
  readFileSync(resolve(uiRoot, 'src/styles/skins/aurora.css'), 'utf8'),
  readFileSync(resolve(uiRoot, 'src/styles/skins/newsprint.css'), 'utf8'),
].join('\n');

const deadGlobalTokens = [
  'ink-soft-10',
  'glass-tint',
  'glass-tint-2',
  'glass-edge',
  'glass-shadow',
  'ease-material',
  'dur-normal',
  'dur-slow',
  'accent-deep',
];

const deadGlobalSelectors = [
  '.func-grid',
  '.func',
  '.player',
  '.np',
  '.transport',
  '.t-btn',
  '.seek',
  '.track',
  '.player-right',
  '.quality',
  '.p-icon',
  '.volume',
  '.dim',
  '.lyric-container',
  '.btn-primary',
  '.btn-secondary',
  '.btn-ghost',
];

function escapeRegex(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function selectorPreludes(source: string): string[] {
  const withoutComments = source.replace(/\/\*[\s\S]*?\*\//g, '');
  return Array.from(withoutComments.matchAll(/(?:^|[{}])\s*(?!@)([^{}]+)\{/g), (match) => match[1]);
}

describe('legacy global CSS cleanup', () => {
  it.each(deadGlobalTokens)('does not redefine the unused --%s token', (token) => {
    expect(globalCss).not.toMatch(new RegExp(`^\\s*--${escapeRegex(token)}\\s*:`, 'm'));
  });

  it('does not retain the unused skin-local --on-accent token', () => {
    expect(skinCss).not.toMatch(/^\s*--on-accent\s*:/m);
  });

  it.each(deadGlobalSelectors)('does not retain the unused %s selector family', (selector) => {
    expect(selectorPreludes(globalCss)).not.toEqual(
      expect.arrayContaining([
        expect.stringMatching(
          new RegExp(`(?:^|[^-\\w])${escapeRegex(selector)}(?=[:.,\\s>{+~#\\[])`),
        ),
      ]),
    );
  });

  it('no longer defines the ambiguous bare .artist rule (resolved ownership)', () => {
    // The bare rule was removed: song-row column layout lives in
    // .song-row .artist, StatsView circular covers live in StatsView scoped.
    expect(globalCss).not.toMatch(/^\s*\.artist\s*\{/m);
  });
});

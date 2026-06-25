import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

/**
 * Music Player.html is a standalone static mockup at the repo root. Its inline
 * <script> must parse as valid JavaScript — a syntax error there kills every
 * interactive handler (play/pause, playSong, details/queue toggles).
 *
 * The #3 bug: the script contained literal escaped backticks
 * `\`url('\${cover}')\`` which made the whole IIFE a SyntaxError.
 */
describe('Music Player.html inline script', () => {
  it('parses as valid JavaScript (no escaped-backtick syntax error)', () => {
    const htmlPath = resolve(__dirname, '../../../../Music Player.html');
    const html = readFileSync(htmlPath, 'utf8');
    const match = html.match(/<script>([\s\S]*?)<\/script>/);
    expect(match, 'expected exactly one <script> block').not.toBeNull();
    const src = match![1];

    // new Function throws SyntaxError on invalid source.
    expect(() => new Function(src)).not.toThrow();
  });
});

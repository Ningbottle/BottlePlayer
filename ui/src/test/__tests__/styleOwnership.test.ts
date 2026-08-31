import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

// Task E3b ownership gate: feature/shell-owned selectors must live in the
// owner CSS file and must no longer remain in the global stylesheet.
// Source-level on purpose — no rendering, so it stays cheap and
// cascade-neutral. Mirrors the prelude extraction used by legacyCssCleanup.
// Suites whose owner stylesheet has not been migrated yet stay skipped so
// each E3b commit is self-consistent; the suite tightens once all three
// owner files exist. Owner files are read inside each test body because
// vitest still runs describe callbacks at collection time even when the
// suite is skipped.
const uiRoot = resolve(__dirname, '../../..');
const ownerFiles = {
  settings: resolve(uiRoot, 'src/features/settings/settings.css'),
  pageRecovery: resolve(uiRoot, 'src/app/shell/pageRecovery.css'),
  lyrics: resolve(uiRoot, 'src/features/lyrics/lyrics.css'),
};
const globalCss = readFileSync(resolve(uiRoot, 'src/style.css'), 'utf8');

function escapeRegex(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function selectorPreludes(source: string): string[] {
  const withoutComments = source.replace(/\/\*[\s\S]*?\*\//g, '');
  return Array.from(
    withoutComments.matchAll(/(?:^|[{}])\s*(?!@)([^{}]+)\{/g),
    (match) => match[1],
  );
}

function hasSelector(preludes: string[], selector: string): boolean {
  const pattern = new RegExp(
    `(?:^|[^-\\w])${escapeRegex(selector)}(?=[:.,\\s>{+~#\\[]|$)`,
  );
  return preludes.some((prelude) =>
    prelude.split(',').some((part) => pattern.test(part.trim())),
  );
}

const globalPreludes = selectorPreludes(globalCss);

describe('feature style ownership (Task E3b)', () => {
  describe(
    'settings feature owns .settings-*/.diag-*/.status-list',
    { skip: !existsSync(ownerFiles.settings) },
    () => {
      const ownerSelectors = [
        '.settings-shell',
        '.settings-nav',
        '.settings-nav-item',
        '.settings-section-title',
        '.settings-hint',
        '.settings-mono',
        '.settings-row',
        '.settings-field',
        '.settings-field-label',
        '.settings-input',
        '.settings-status',
        '.settings-badge',
        '.settings-warn',
        '.settings-confirm',
        '.settings-changelog',
        '.settings-subpanel',
        '.settings-subpanel-head',
        '.settings-subpanel-title',
        '.settings-empty',
        '.settings-preformatted',
        '.settings-diag-list',
        '.settings-content',
        '.diag-row',
        '.diag-ts',
        '.diag-kind',
        '.diag-phase',
        '.diag-detail',
        '.diag-track',
        '.status-list',
      ];

      it.each(ownerSelectors)(
        '%s is owned by settings.css and gone from style.css',
        (selector) => {
          const ownerPreludes = selectorPreludes(
            readFileSync(ownerFiles.settings, 'utf8'),
          );
          expect(hasSelector(ownerPreludes, selector)).toBe(true);
          expect(hasSelector(globalPreludes, selector)).toBe(false);
        },
      );

      it('keeps the aurora and newsprint settings variants beside the feature', () => {
        const settingsCss = readFileSync(ownerFiles.settings, 'utf8');
        expect(settingsCss).toContain(
          ':root[data-skin="aurora"] .settings-content',
        );
        expect(settingsCss).toContain(
          ':root[data-skin="newsprint"] .settings-content',
        );
      });
    },
  );

  describe(
    'app/shell owns .page-recovery*',
    { skip: !existsSync(ownerFiles.pageRecovery) },
    () => {
      const ownerSelectors = [
        '.page-recovery',
        '.page-recovery__panel',
        '.page-recovery__eyebrow',
        '.page-recovery__title',
        '.page-recovery__message',
        '.page-recovery__actions',
        '.page-recovery__button',
        '.page-recovery__button--primary',
      ];

      it.each(ownerSelectors)(
        '%s is owned by pageRecovery.css and gone from style.css',
        (selector) => {
          const ownerPreludes = selectorPreludes(
            readFileSync(ownerFiles.pageRecovery, 'utf8'),
          );
          expect(hasSelector(ownerPreludes, selector)).toBe(true);
          expect(hasSelector(globalPreludes, selector)).toBe(false);
        },
      );
    },
  );

  describe(
    'lyrics feature owns lyric page selectors',
    { skip: !existsSync(ownerFiles.lyrics) },
    () => {
      const ownerSelectors = [
        '.lyric-meta',
        '.lyric-right',
        '.lyric-scroll',
        '.lyric-line',
      ];

      it.each(ownerSelectors)(
        '%s is owned by lyrics.css and gone from style.css',
        (selector) => {
          const ownerPreludes = selectorPreludes(
            readFileSync(ownerFiles.lyrics, 'utf8'),
          );
          expect(hasSelector(ownerPreludes, selector)).toBe(true);
          expect(hasSelector(globalPreludes, selector)).toBe(false);
        },
      );

      it('keeps html.lyric-left and dark-mode lyric overrides beside the feature', () => {
        const lyricsCss = readFileSync(ownerFiles.lyrics, 'utf8');
        expect(lyricsCss).toContain('html.lyric-left .lyric-line');
        expect(lyricsCss).toContain(':root[data-mode="dark"] .lyric-right');
        expect(lyricsCss).toContain(
          ':root[data-mode="dark"] .lyric-right::-webkit-scrollbar-thumb',
        );
        expect(lyricsCss).toContain(
          ':root[data-mode="dark"] .lyric-meta .big-cover',
        );
      });

      it('keeps the lyric scrollbar sizing rules beside the feature', () => {
        const lyricsCss = readFileSync(ownerFiles.lyrics, 'utf8');
        expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar');
        expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar-thumb');
        expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar-track');
      });
    },
  );
});

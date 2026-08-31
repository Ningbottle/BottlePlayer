import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

// Task E3b ownership gate: feature/shell-owned selectors must live in the
// owner CSS file and must no longer remain in the global stylesheet.
// Source-level on purpose — no rendering, so it stays cheap and
// cascade-neutral. Mirrors the prelude extraction used by legacyCssCleanup.
// Owner files are read unconditionally: a missing owner stylesheet must fail
// the gate, and so must a broken main.ts import order (cascade contract).
const uiRoot = resolve(__dirname, '../../..');
const globalCss = readFileSync(resolve(uiRoot, 'src/style.css'), 'utf8');
const settingsCss = readFileSync(
  resolve(uiRoot, 'src/features/settings/settings.css'),
  'utf8',
);
const pageRecoveryCss = readFileSync(
  resolve(uiRoot, 'src/app/shell/pageRecovery.css'),
  'utf8',
);
const lyricsCss = readFileSync(
  resolve(uiRoot, 'src/features/lyrics/lyrics.css'),
  'utf8',
);
const mainTs = readFileSync(resolve(uiRoot, 'src/main.ts'), 'utf8');

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
  it('loads the owner stylesheets between style.css and the skins, once each', () => {
    // main.ts decides the cascade; the owner files must sit after style.css
    // and before both skin stylesheets, exactly once each.
    const cssImportOrder: string[] = [];
    for (const match of mainTs.matchAll(/^import\s+"(\.\/[^"]+\.css)";?/gm)) {
      cssImportOrder.push(match[1]);
    }
    expect(cssImportOrder).toEqual([
      './styles/tokens.css',
      './styles/progress.css',
      './style.css',
      './features/settings/settings.css',
      './app/shell/pageRecovery.css',
      './features/lyrics/lyrics.css',
      './styles/skins/aurora.css',
      './styles/skins/newsprint.css',
    ]);
  });

  describe('settings feature owns .settings-*/.diag-*/.status-list', () => {
    const ownerPreludes = selectorPreludes(settingsCss);
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
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        expect(hasSelector(globalPreludes, selector)).toBe(false);
      },
    );

    it('keeps the aurora and newsprint settings variants beside the feature', () => {
      expect(settingsCss).toContain(
        ':root[data-skin="aurora"] .settings-content',
      );
      expect(settingsCss).toContain(
        ':root[data-skin="newsprint"] .settings-content',
      );
    });
  });

  describe('app/shell owns .page-recovery*', () => {
    const ownerPreludes = selectorPreludes(pageRecoveryCss);
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
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        expect(hasSelector(globalPreludes, selector)).toBe(false);
      },
    );
  });

  describe('lyrics feature owns lyric page selectors', () => {
    const ownerPreludes = selectorPreludes(lyricsCss);
    const ownerSelectors = [
      '.lyric-meta',
      '.lyric-right',
      '.lyric-scroll',
      '.lyric-line',
    ];

    it.each(ownerSelectors)(
      '%s is owned by lyrics.css and gone from style.css',
      (selector) => {
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        // The shared compact-mode group keeps html.compact .lyric-scroll in
        // style.css (style-ownership §3), so only a bare .lyric-scroll
        // prelude would mean the feature rule was left behind.
        const bareSelectorLeft =
          selector === '.lyric-scroll' &&
          globalPreludes.some((prelude) => prelude.trim() === '.lyric-scroll');
        expect(bareSelectorLeft || hasSelector(globalPreludes, selector)).toBe(
          selector === '.lyric-scroll',
        );
      },
    );

    it('keeps html.lyric-left and dark-mode lyric overrides beside the feature', () => {
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
      expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar');
      expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar-thumb');
      expect(lyricsCss).toContain('.lyric-right::-webkit-scrollbar-track');
    });
  });
});

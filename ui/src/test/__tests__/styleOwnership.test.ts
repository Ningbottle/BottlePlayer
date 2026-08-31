import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

// Task E3b/E3c ownership gate: feature/shell-owned selectors must live in
// the owner CSS file and must no longer remain in the global stylesheet.
// Source-level on purpose — no rendering, so it stays cheap and
// cascade-neutral. Mirrors the prelude extraction used by legacyCssCleanup.
// Owner files are read unconditionally: a missing owner stylesheet must fail
// the gate, and so must a broken main.ts import order (cascade contract).
const uiRoot = resolve(__dirname, '../../..');
const globalCss = readFileSync(resolve(uiRoot, 'src/styles/base.css'), 'utf8');
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
const shellCss = readFileSync(resolve(uiRoot, 'src/app/shell/shell.css'), 'utf8');
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
  it('loads the owner stylesheets between base.css and the skins, once each', () => {
    // main.ts decides the cascade; the owner files must sit after base.css
    // and before both skin stylesheets, exactly once each.
    const cssImportOrder: string[] = [];
    for (const match of mainTs.matchAll(/^import\s+"(\.\/[^"]+\.css)";?/gm)) {
      cssImportOrder.push(match[1]);
    }
    expect(cssImportOrder).toEqual([
      './styles/tokens.css',
      './styles/progress.css',
      './styles/base.css',
      './app/shell/shell.css',
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
      '%s is owned by settings.css and gone from base.css',
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
      '%s is owned by pageRecovery.css and gone from base.css',
      (selector) => {
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        expect(hasSelector(globalPreludes, selector)).toBe(false);
      },
    );
  });

  describe('app/shell owns shell chrome (Task E3c)', () => {
    const ownerPreludes = selectorPreludes(shellCss);
    // Each entry: the bare/owned prelude that must live in shell.css.
    // html.compact .playlists a stays in base.css with the shared compact
    // group (style-ownership §3), so bare preludes are asserted instead of
    // substring matching which would also hit the compound selectors.
    const ownerSelectors = [
      '.app',
      '.sidebar',
      '.sidebar::after',
      '.masthead',
      '.user',
      '.avatar',
      '.section-label',
      '.nav',
      '.playlists',
      '.playlist-placeholder',
      '.playlist-retry',
      '.sidebar-footer',
      '.main',
      '.topbar',
      '.nav-arrows',
      '.icon-btn',
      '.search',
      '.free-badge',
      '.top-actions',
      '.titlebar',
      '.titlebar-logo',
      '.titlebar-center',
      '.titlebar-controls',
      '.app.lyric-fullscreen-active',
    ];

    it.each(ownerSelectors)(
      '%s is owned by shell.css and gone from base.css',
      (selector) => {
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        // The shared compact-mode group keeps html.compact .playlists a in
        // base.css (style-ownership §3), so only a bare .playlists prelude
        // would mean the shell rule was left behind.
        const bareSelectorLeft =
          selector === '.playlists' &&
          globalPreludes.some((prelude) => prelude.trim() === '.playlists');
        expect(bareSelectorLeft || hasSelector(globalPreludes, selector)).toBe(
          selector === '.playlists',
        );
      },
    );

    it('keeps the shell dark-mode overrides beside the shell', () => {
      const mustContain = [
        ':root[data-mode="dark"] .nav a:hover',
        ':root[data-mode="dark"] .nav a.active',
        ':root[data-mode="dark"] .playlists a:hover',
        ':root[data-mode="dark"] .icon-btn',
        ':root[data-mode="dark"] .icon-btn:hover',
        ':root[data-mode="dark"] .titlebar-controls .control-btn:hover',
        ':root[data-mode="dark"] .sidebar',
        ':root[data-mode="dark"] .topbar',
        ':root[data-mode="dark"] .nav-arrows button',
        ':root[data-mode="dark"] .search',
      ];
      for (const fragment of mustContain) {
        expect(shellCss).toContain(fragment);
        expect(globalCss).not.toContain(fragment);
      }
    });

    it('keeps the aurora paper-layer hiding in base.css (shell background, §3)', () => {
      expect(globalCss).toContain(':root[data-skin="aurora"] .paper-base');
      expect(shellCss).not.toContain(':root[data-skin="aurora"] .paper-base');
    });
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
      '%s is owned by lyrics.css and gone from base.css',
      (selector) => {
        expect(hasSelector(ownerPreludes, selector)).toBe(true);
        // The shared compact-mode group keeps html.compact .lyric-scroll in
        // base.css (style-ownership §3), so only a bare .lyric-scroll
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

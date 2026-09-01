import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';
import { compileStyle } from 'vue/compiler-sfc';

// Phase E correction gates. These operate on COMPILED scoped CSS (via
// @vue/compiler-sfc), not source strings: the E3c/E3d batch proved that
// partial `:global(:root) .desc` forms silently rewrite the selector in
// compiled output, so only compiled rules are trustworthy.

const uiRoot = resolve(__dirname, '../../..');

function compileScoped(vuePath: string, id: string): string {
  const source = readFileSync(vuePath, 'utf8');
  const styleMatch = source.match(/<style[^>]*>/);
  if (!styleMatch) throw new Error(`no <style> block in ${vuePath}`);
  const styleSource = source.slice(
    source.indexOf(styleMatch[0]) + styleMatch[0].length,
    source.lastIndexOf('</style>'),
  );
  const result = compileStyle({
    id,
    filename: vuePath,
    source: styleSource,
    scoped: true,
  });
  if (result.errors.length) throw new Error(result.errors.join('; '));
  return result.code;
}

function rules(compiled: string): { selector: string; body: string }[] {
  const out: { selector: string; body: string }[] = [];
  for (const m of compiled.matchAll(/([^{}]+)\{([^{}]*)\}/g)) {
    // Strip scope attributes and leading comments so exact-selector
    // assertions stay stable.
    const selector = m[1]
      .replace(/\/\*[^*]*\*\//g, '')
      .replace(/\[data-v-[^\]]+\]/g, '')
      .trim();
    out.push({ selector, body: m[2].trim() });
  }
  return out;
}

const newsprintHomeCompiled = compileScoped(
  resolve(uiRoot, 'src/features/home/NewsprintHome.vue'),
  'data-v-nphome',
);
const queuePanelCompiled = compileScoped(
  resolve(uiRoot, 'src/playback/components/QueuePanel.vue'),
  'data-v-qpanel',
);
const baseCss = readFileSync(resolve(uiRoot, 'src/styles/base.css'), 'utf8');
const tokensCss = readFileSync(resolve(uiRoot, 'src/styles/tokens.css'), 'utf8');

function tokenNames(css: string): string[] {
  return Array.from(css.matchAll(/^\s*--([A-Za-z0-9-]+)\s*:/gm), (m) => m[1]);
}

describe('compiled dark-home ownership (correction 1)', () => {
  const homeDarkRules = rules(newsprintHomeCompiled).filter((r) =>
    r.selector.includes('data-mode'),
  );

  it('compiles at least one dark rule (the migration must be present)', () => {
    expect(homeDarkRules.length).toBeGreaterThan(0);
  });

  it.each(['.card', '.feature', '.side-list'])(
    'every compiled dark rule touching %s is qualified by .np-home',
    (fragment) => {
      const offenders = homeDarkRules.filter(
        (r) => r.selector.includes(fragment) && !r.selector.includes('.np-home'),
      );
      expect(
        offenders.map((r) => r.selector),
        `unqualified dark selector(s) for ${fragment}`,
      ).toEqual([]);
    },
  );

  it('never paints the document root background from the home component', () => {
    const rootRules = rules(newsprintHomeCompiled).filter(
      (r) =>
        r.selector.replace(/\/\*[^*]*\*\//g, '').trim() === ":root[data-mode='dark']" &&
        r.body.includes('background'),
    );
    expect(
      rootRules.map((r) => r.body),
      'line 552 partial :global compiles to a root background rule',
    ).toEqual([]);
  });

  it('keeps the dark side-list hover at the original rgba(255,255,255,0.04)', () => {
    const hover = homeDarkRules.filter((r) => r.selector.includes('.side-list li:hover'));
    expect(hover.length).toBeGreaterThan(0);
    for (const r of hover) expect(r.body).toContain('255, 255, 255, 0.04');
  });
});

describe('QueuePanel single-owner .recent (correction 2)', () => {
  const panelRules = rules(queuePanelCompiled);

  it('defines bare ".recent .item" exactly once', () => {
    const exact = panelRules.filter((r) => r.selector === '.recent .item');
    expect(exact.map((r) => r.selector)).toEqual(['.recent .item']);
  });

  it('defines ".recent" exactly once', () => {
    const exact = panelRules.filter((r) => r.selector === '.recent');
    expect(exact.map((r) => r.selector)).toEqual(['.recent']);
  });

  it('keeps the owner-block layout (flex row, gap 12px, padding 8px 16px)', () => {
    const item = panelRules.find((r) => r.selector === '.recent .item');
    expect(item).toBeDefined();
    expect(item!.body).toContain('display: flex');
    expect(item!.body).toContain('gap: 12px');
    expect(item!.body).toContain('padding: 8px 16px');
  });

  it('keeps one light hover rgba(255,255,255,0.3) and no warm duplicate', () => {
    const hovers = panelRules.filter((r) => r.selector === '.recent .item:hover');
    expect(hovers).toHaveLength(1);
    expect(hovers[0].body.replace(/\s+/g, '')).toContain('rgba(255,255,255,0.3)');
  });

  it('defines each dark recent rule at most once', () => {
    const darkHovers = panelRules.filter(
      (r) => r.selector.includes('data-mode') && r.selector.includes('.recent .item:hover'),
    );
    expect(darkHovers).toHaveLength(1);
    expect(darkHovers[0].body.replace(/\s+/g, '')).toContain('rgba(255,255,255,0.06)');
  });

  it('qualifies every compiled dark rule with the .queue-panel owner', () => {
    const darkRules = panelRules.filter((r) => r.selector.includes('data-mode'));
    expect(darkRules.length).toBeGreaterThan(0);
    const unowned = darkRules.filter((r) => !r.selector.includes('.queue-panel'));
    expect(
      unowned.map((r) => r.selector),
      'dark rules without the .queue-panel owner leak panel styles globally',
    ).toEqual([]);
  });
});

describe('bare .artist removal (correction 3)', () => {
  it('base.css no longer defines the ambiguous bare .artist rule', () => {
    expect(baseCss).not.toMatch(/^\s*\.artist\s*\{/m);
  });

  it('song-row artist column keeps its layout via .song-row .artist', () => {
    expect(baseCss).toMatch(/\.song-row \.artist[^{]*\{/);
  });

  it('StatsView keeps owning the circular-cover semantics', () => {
    const stats = readFileSync(resolve(uiRoot, 'src/features/stats/StatsView.vue'), 'utf8');
    expect(stats).toMatch(/\.top-cover\.artist\s*\{/);
  });
});

describe('token single-ownership (correction 4)', () => {
  const shared = tokenNames(baseCss).filter((t) => tokenNames(tokensCss).includes(t));

  it('base.css and tokens.css have no token-name overlap', () => {
    expect(shared).toEqual([]);
  });

  it('--accent is defined only in tokens.css', () => {
    expect(baseCss).not.toMatch(/^\s*--accent\s*:/m);
    expect(tokensCss).toMatch(/^\s*--accent\s*:/m);
  });
});

# Aurora 字体 + 顶部栏合并 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Aurora 改用 Inter+Noto Sans SC（web 加载）修复 Windows 丑字体；非播放页把标题栏+顶栏合并为大页面头，窗口控件浮右上角；两皮肤一致。

**Architecture:** 字体：index.html 加 Google Fonts 链接 + style.css Aurora 块换字体栈。顶部栏：App.vue 推导 `isPlaybackView`（/lyric 路由）-> 传 shell prop -> `.app` 上 `data-header="merged"|"compact"` -> aurora.css/newsprint.css 据 `data-header="merged"` 重排首行 + 窗口控件绝对定位。CSS 驱动为主，Vue 侧只加属性。

**Tech Stack:** Vue 3 + vue-router + Vitest + CSS。

## Global Constraints

- 在主工作树 `C:/BottleMusic` 的 `main` 分支直接实施。git 用 `git -C "C:/BottleMusic"`。
- 不改 Newsprint 字体。不改播放页(/lyric 非全屏)与全屏的标题栏行为。不改 C++/Rust/安全代码。
- TDD：先写失败测试再实现。每任务独立提交。
- 基线不得回退：Vitest 787+、vue-tsc、vite build 绿；`Shells.test.ts`、`FullscreenWindowControls.test.ts` 不回退。
- `docs/` 被 gitignore，spec/plan/report 用 `git add -f`。
- Noto Sans SC 体积大，Google Fonts 链接用 `display=swap` 异步加载，不阻塞首屏。

---

## Task 1: Aurora 字体修复（index.html + style.css）

**Files:**
- Modify: `ui/index.html:9` (Google Fonts link)
- Modify: `ui/src/style.css:80-83` (Aurora `--font-serif`/`--font-sans`)
- Test: `ui/src/api/__tests__/auroraFont.test.ts` (new)

- [ ] **Step 1: Write the failing test**

Create `ui/src/api/__tests__/auroraFont.test.ts`:
```ts
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
```

- [ ] **Step 2: Run test to verify it fails**

`cd ui && pnpm vitest run src/api/__tests__/auroraFont.test.ts`
Expected: FAIL - index.html lacks Inter; style.css Aurora `--font-sans` contains SF Pro.

- [ ] **Step 3: Add Inter + Noto Sans SC to index.html**

Replace the font `<link>` at `ui/index.html:9` with:
```html
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=Noto+Sans+SC:wght@400;500;700&family=ZCOOL+XiaoWei&display=swap" rel="stylesheet" />
```

- [ ] **Step 4: Change Aurora font stack in style.css**

In `ui/src/style.css`, the `:root[data-skin="aurora"]` block (~lines 80-83), replace both `--font-serif` and `--font-sans` with:
```css
  --font-serif:
    "Inter", "Noto Sans SC", system-ui, -apple-system, sans-serif;
  --font-sans:
    "Inter", "Noto Sans SC", system-ui, -apple-system, sans-serif;
```

- [ ] **Step 5: Run test to verify pass**

`cd ui && pnpm vitest run src/api/__tests__/auroraFont.test.ts`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git -C "C:/BottleMusic" add ui/index.html ui/src/style.css ui/src/api/__tests__/auroraFont.test.ts
git -C "C:/BottleMusic" commit -m "fix(ui): Aurora uses Inter + Noto Sans SC web fonts instead of SF Pro"
```

---

## Task 2: isPlaybackView prop + data-header 属性

**Files:**
- Modify: `ui/src/App.vue` (add `useRoute`, `isPlaybackView` computed, pass prop)
- Modify: `ui/src/components/shell/AuroraShell.vue` (prop + `data-header`)
- Modify: `ui/src/components/shell/NewsprintShell.vue` (prop + `data-header`)
- Test: `ui/src/components/shell/__tests__/headerMerge.test.ts` (new)

**Interfaces:**
- Produces: shells accept `isPlaybackView?: boolean`; `.app` gets `data-header="merged"` when `!isPlaybackView && !lyricFullscreen`, else `"compact"`.

- [ ] **Step 1: Write the failing test**

Create `ui/src/components/shell/__tests__/headerMerge.test.ts`:
```ts
import { describe, it, expect } from 'vitest';
import { mount } from '@vue/test-utils';
import AuroraShell from '../AuroraShell.vue';

// Minimal stubs: AuroraShell renders slots + WindowControls (which calls Tauri).
// Mount with global stubs to avoid Tauri invoke.
const stubs = { WindowControls: { template: '<div class="wc-stub" />' } };

function makeShell(props: Record<string, unknown>) {
  return mount(AuroraShell, { props, global: { stubs } });
}

describe('shell data-header attribute', () => {
  it('non-playback, non-fullscreen -> merged', () => {
    const w = makeShell({ isPlaybackView: false, lyricFullscreen: false });
    expect(w.find('.app').attributes('data-header')).toBe('merged');
  });

  it('playback view (/lyric) -> compact', () => {
    const w = makeShell({ isPlaybackView: true, lyricFullscreen: false });
    expect(w.find('.app').attributes('data-header')).toBe('compact');
  });

  it('lyric fullscreen -> compact', () => {
    const w = makeShell({ isPlaybackView: false, lyricFullscreen: true });
    expect(w.find('.app').attributes('data-header')).toBe('compact');
  });
});
```
(If `WindowControls` import path/stub needs adjustment, mirror how `Shells.test.ts` stubs it.)

- [ ] **Step 2: Run test to verify it fails**

`cd ui && pnpm vitest run src/components/shell/__tests__/headerMerge.test.ts`
Expected: FAIL - `data-header` attribute absent.

- [ ] **Step 3: Add prop + data-header to AuroraShell.vue**

In `AuroraShell.vue`:
- `withDefaults(defineProps<{ lyricFullscreen?: boolean; isPlaybackView?: boolean }>(), { lyricFullscreen: false, isPlaybackView: false });`
- On the `.app` div, add: `:data-header="(!isPlaybackView && !lyricFullscreen) ? 'merged' : 'compact'"`.

- [ ] **Step 4: Mirror in NewsprintShell.vue**

Read `NewsprintShell.vue`; add the same `isPlaybackView` prop and `:data-header` binding on its `.app` div.

- [ ] **Step 5: Wire isPlaybackView in App.vue**

In `App.vue`:
- Add `useRoute` to the vue-router import: `import { RouterView, useRouter, useRoute } from 'vue-router';`
- Add `const route = useRoute();` near `appRouter`.
- Add `const isPlaybackView = computed(() => route.path.startsWith('/lyric'));`
- Where the shell is rendered (the `:lyric-fullscreen="lyricFullscreen"` line ~130), add `:is-playback-view="isPlaybackView"`.

- [ ] **Step 6: Run test to verify pass + existing shell tests**

`cd ui && pnpm vitest run src/components/shell/__tests__/headerMerge.test.ts src/components/shell/__tests__/Shells.test.ts`
Expected: PASS (new tests green; Shells.test.ts not regressed - if it asserts on `.app` attributes and breaks, update it with a new contract assertion and note in commit).

- [ ] **Step 7: Commit**

```bash
git -C "C:/BottleMusic" add ui/src/App.vue ui/src/components/shell/AuroraShell.vue ui/src/components/shell/NewsprintShell.vue ui/src/components/shell/__tests__/headerMerge.test.ts
git -C "C:/BottleMusic" commit -m "feat(ui): add isPlaybackView prop and data-header attribute to shells"
```

---

## Task 3: 合并头 CSS（aurora.css + newsprint.css）

**Files:**
- Modify: `ui/src/styles/skins/aurora.css`
- Modify: `ui/src/styles/skins/newsprint.css`

**Interfaces:** Consumes `data-header="merged"` on `.app[data-shell="..."]`. Produces merged ~60px header row + absolute window controls.

- [ ] **Step 1: Implement merged header CSS in aurora.css**

Add to `aurora.css` (after the existing titlebar rules, ~line 115):
```css
/* Merged big page header on non-playback pages */
.app[data-shell="aurora"][data-header="merged"] {
  grid-template-rows: 60px auto minmax(0, 1fr) 104px;
}

[data-shell="aurora"][data-header="merged"] .titlebar {
  grid-row: 1 / 2;
  grid-column: 1 / 3;
  display: flex;
  align-items: center;
  padding: 0 140px 0 16px; /* right room for floating window controls */
}

[data-shell="aurora"][data-header="merged"] .titlebar-controls {
  position: absolute;
  top: 0;
  right: 0;
  height: 60px;
}

[data-shell="aurora"][data-header="merged"] .aurora-wordmark {
  font-size: 17px;
  font-weight: 600;
  letter-spacing: 0.01em;
}

/* Merged header shares content background, no divider (already transparent). */
[data-shell="aurora"][data-header="merged"] .shell-topbar {
  border: 0;
  background: transparent;
}
```
Adjust the `padding-right` and wordmark size during QA if the search/topbar overflow. Keep `data-tauri-drag-region` working (titlebar already has it; controls `@mousedown.stop`).

- [ ] **Step 2: Mirror merged header CSS in newsprint.css**

Read `newsprint.css` titlebar rules; add an analogous `.app[data-shell="newsprint"][data-header="merged"]` block (60px row, absolute controls, bigger masthead wordmark). Match Newsprint's existing titlebar styling (paper background, masthead).

- [ ] **Step 3: Run full Vitest + type-check + build**

```bash
cd "C:/BottleMusic/ui" && pnpm vitest run && pnpm vue-tsc --noEmit && pnpm build
```
Expected: all green (CSS changes don't break unit tests; build succeeds).

- [ ] **Step 4: Commit**

```bash
git -C "C:/BottleMusic" add ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git -C "C:/BottleMusic" commit -m "feat(ui): merge titlebar+topbar into big header on non-playback pages"
```

---

## Task 4: Dev server QA + final verification

- [ ] **Step 1: Start dev server and visually QA**

Start the vite-dev preview server (`.claude/launch.json` already has `vite-dev`). Open `http://localhost:1420/?layoutDemo=1`.
- Aurora home: confirm font is Inter/Noto Sans SC (NOT YaHei); confirm merged big header (wordmark + topbar in one ~60px row, window controls top-right floating).
- Switch to Newsprint (localStorage `appearance_skin=newsprint` + reload): confirm merged header + serif font unchanged.
- Navigate to `/lyric?layoutDemo=1`: confirm compact titlebar (NOT merged) - `data-header="compact"`.
- Confirm window controls still clickable; titlebar drag region still works (can't test drag in browser, but controls clickable).

- [ ] **Step 2: Fix any visual issues found**

Adjust CSS (wordmark size, padding, row height) based on QA. Re-verify.

- [ ] **Step 3: Final full verification**

```bash
cd "C:/BottleMusic/ui" && pnpm vitest run && pnpm vue-tsc --noEmit && pnpm build
```
Expected: all green.

- [ ] **Step 4: Commit any QA fixes + push**

```bash
git -C "C:/BottleMusic" add -p   # stage QA fix files
git -C "C:/BottleMusic" commit -m "fix(ui): adjust merged header spacing from QA"
git -C "C:/BottleMusic" push origin main
```
(Push only if user confirmed; the user said continue, so push is authorized for this task.)

---

## Self-Review Notes

- Spec coverage: font -> Task 1; isPlaybackView + data-header -> Task 2; merged CSS -> Task 3; QA + verify -> Task 4. All spec sections covered.
- Task 2 test stubs `WindowControls` to avoid Tauri invoke; mirror `Shells.test.ts`'s stubbing approach if the stub as written doesn't compile.
- Task 3 CSS values (60px, 17px, 140px padding) are starting points; QA (Task 4) calibrates. The `data-tauri-drag-region` + `@mousedown.stop` on controls must be preserved.
- NewsprintShell.vue not pre-read; Task 2 Step 4 reads it before mirroring.
- index.html FOUC script reads legacy `tweak_skin` (pre-existing, out of scope) - not touched.

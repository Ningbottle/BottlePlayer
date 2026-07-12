# Dual-Interface Player Redesign Closeout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task with review checkpoints. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish dual-interface visual closeout on the redesign worktree so Aurora and Newsprint are clearly distinct, pass regression + minimum visual matrix, then merge cleanly into `main`.

**Architecture:** Keep shared controllers (`playerStore`, `homeFeedStore`, `viewRegistry`, lyric, window commands). Keep independent Home / PlayerBar / LyricStage. Close gaps by structurally differentiating Sidebar/Topbar via `data-skin` / root attributes (one business component, skin-specific DOM markers + CSS), enriching empty queue density, finishing secondary-page primitives where still on legacy `.page-head`, then verifying and merging.

**Tech Stack:** Vue 3 Composition API, TypeScript, Tauri 2, GSAP 3.15, Vitest 4, Vue Test Utils, Vite 6, CSS custom properties.

**Worktree (mandatory for all UI work):**

```
C:\BottleMusic\.worktrees\dual-interface-player-redesign
Branch: codex/dual-interface-player-redesign
```

All `cd ui` commands below mean:

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
```

**Spec:** `docs/superpowers/specs/2026-07-12-dual-interface-closeout-design.md`  
**Product design (truth source):** `docs/superpowers/specs/2026-07-10-dual-interface-player-redesign-design.md`

## Global Constraints

- Do not modify Rust, C++, AudioProxy, EQ DSP, stats schema, login protocol, or backend response shapes.
- Do not introduce Motion/Framer Motion or new animation libraries; use existing GSAP + `motionProfiles`.
- Play, seek, and navigate must happen first; animation must not delay business actions.
- One business implementation for Sidebar/Topbar — no duplicated playlist/update/login API calls per skin.
- Do not commit `node_modules`, `target/`, DLL build products, or large binary screenshot dumps unless the verification task explicitly requires a small tracked report only.
- Red → Green → Refactor per task; commit only that task’s files.
- Never force-push `main` or `origin`.

## File Map

| Path | Responsibility |
|---|---|
| `ui/src/components/Sidebar.vue` | Shared nav business; add `data-skin` structural markers |
| `ui/src/components/Topbar.vue` | Shared search/actions; add skin structural markers |
| `ui/src/styles/skins/aurora.css` | Aurora shell + chrome structural styles |
| `ui/src/styles/skins/newsprint.css` | Newsprint shell + chrome structural styles |
| `ui/src/views/home/AuroraHome.vue` | Stage + queue rail empty-state density |
| `ui/src/views/home/NewsprintHome.vue` | Newsprint home empty/queue density if present |
| `ui/src/views/SearchView.vue` | Migrate legacy `.page-head` → skin primitives |
| `ui/src/views/PlaylistView.vue` | Migrate legacy `.page-head` → skin primitives |
| `ui/src/views/StatsView.vue` | Density pass if still card-heavy (already uses SkinPageHeader) |
| `ui/src/components/primitives/*` | Existing Skin* primitives (reuse, do not reinvent) |
| `docs/superpowers/reports/2026-07-12-dual-interface-closeout-gap.md` | P1 gap list |
| `docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md` | Task 10 verification report |
| `CONTEXT.md` | Post-merge status (on merge task) |

---

### Task 1: Environment check + written gap audit

**Files:**
- Create: `docs/superpowers/reports/2026-07-12-dual-interface-closeout-gap.md`
- Read only: design QA under `ui/design-qa-captures/`, key components listed above

**Interfaces:**
- Consumes: closeout design §4 P0/P1
- Produces: gap table with severity `high|medium|low` and disposition `do|defer|wont`

- [ ] **Step 1: Confirm worktree branch and that redesign sources exist**

```powershell
git -C C:\BottleMusic\.worktrees\dual-interface-player-redesign rev-parse --abbrev-ref HEAD
git -C C:\BottleMusic\.worktrees\dual-interface-player-redesign status -sb
Test-Path C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui\src\views\home\AuroraHome.vue
Test-Path C:\BottleMusic\ui\src\views\home\AuroraHome.vue
```

Expected:
- Branch name is `codex/dual-interface-player-redesign`
- Worktree `AuroraHome.vue` is `$true`
- Main workspace `AuroraHome.vue` is `$false` (documents why main looks unchanged)

- [ ] **Step 2: Write the gap report**

Create `docs/superpowers/reports/2026-07-12-dual-interface-closeout-gap.md` with at least:

```markdown
# Dual-interface closeout gap list

Date: 2026-07-12
Branch: codex/dual-interface-player-redesign

## Environment
- Worktree path: ...
- Main has redesign UI: no
- Default skin: aurora (themeStore)

## Gaps

| ID | Severity | Area | Observation | Disposition |
|----|----------|------|-------------|-------------|
| G1 | high | Sidebar | Single Newsprint-leaning DOM (masthead "The Player", stamp footer); no data-skin structural split | do (Task 2) |
| G2 | high | Topbar | Shared topbar classes; search/actions not skin-differentiated | do (Task 3) |
| G3 | high | Queue empty | `.aurora-queue-empty` only shows "暂无队列"; QA railRows often 0 | do (Task 4) |
| G4 | medium | Search/Playlist | Still use `.page-head` | do (Task 5) |
| G5 | medium | Stats density | SkinPageHeader present; may still feel sparse | do if screenshot fails density (Task 5) |
| G6 | low | Pixel polish | Proportion tweaks after matrix | defer unless matrix fails |

## Out of scope
- Backend / AudioProxy / EQ / stats protocol
```

Fill observations by reading `Sidebar.vue`, `Topbar.vue`, `AuroraHome.vue` empty block, `SearchView.vue`, `PlaylistView.vue`, and `manifest.json` `railRows` notes.

- [ ] **Step 3: Force-add and commit (docs/ is gitignored)**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add -f docs/superpowers/reports/2026-07-12-dual-interface-closeout-gap.md
git -C $wt commit -m "docs: dual-interface closeout gap audit"
```

---

### Task 2: Sidebar structural skin differentiation

**Files:**
- Modify: `ui/src/components/Sidebar.vue`
- Modify: `ui/src/styles/skins/aurora.css`
- Modify: `ui/src/styles/skins/newsprint.css`
- Modify: `ui/src/components/__tests__/Sidebar.test.ts`
- Optional create: `ui/src/components/__tests__/Sidebar.skin.test.ts` if keeping existing tests untouched is cleaner

**Interfaces:**
- Consumes: `useThemeStore().skinId` (`'aurora' | 'newsprint'`)
- Produces: root element `data-skin-chrome="aurora|newsprint"`; Aurora active nav uses pill marker; Newsprint active nav uses numbered index + left rule (not pill)
- Does not produce: second playlist loader or second update-check path

- [ ] **Step 1: Write failing skin-structure tests**

Append to `ui/src/components/__tests__/Sidebar.test.ts` (keep existing mocks) or new file with same mocks:

```ts
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Sidebar from '../Sidebar.vue';
import { useThemeStore, __resetForTest } from '../../api/themeStore';

// reuse the same vi.mock blocks as existing Sidebar.test.ts for updater/backend/userStore/favorite/skippedVersion

describe('Sidebar skin chrome', () => {
  beforeEach(() => {
    __resetForTest();
    localStorage.clear();
    useThemeStore().init();
  });

  it('marks chrome for aurora and uses pill active nav without newsprint stamp footer', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Sidebar, { props: { activeView: 'home' } });
    await nextTick();
    const root = wrapper.get('[data-test="sidebar-chrome"]');
    expect(root.attributes('data-skin-chrome')).toBe('aurora');
    expect(wrapper.find('[data-test="sidebar-nav-active-pill"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="newsprint-stamp"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="aurora-nav-label"]').exists()).toBe(true);
  });

  it('marks chrome for newsprint with numbered nav and stamp footer, no aurora pill', async () => {
    useThemeStore().setSkin('newsprint');
    const wrapper = mount(Sidebar, { props: { activeView: 'home' } });
    await nextTick();
    const root = wrapper.get('[data-test="sidebar-chrome"]');
    expect(root.attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.find('[data-test="sidebar-nav-index"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="newsprint-stamp"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="sidebar-nav-active-pill"]').exists()).toBe(false);
  });

  it('still emits navigate without duplicating API surface', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Sidebar, { props: { activeView: 'home' } });
    await wrapper.findAll('.nav a, [data-test="sidebar-nav-item"]')[0].trigger('click');
    expect(wrapper.emitted('navigate')?.[0]).toEqual(['home']);
  });
});
```

- [ ] **Step 2: Run tests and confirm failure**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/components/__tests__/Sidebar.test.ts
```

Expected: FAIL — missing `data-test="sidebar-chrome"` / skin markers (not mock/setup errors).

- [ ] **Step 3: Minimal Sidebar implementation**

In `Sidebar.vue`:

1. Import `useThemeStore`.
2. `const themeStore = useThemeStore(); const skinId = themeStore.skinId;`
3. On root `<aside>`:

```vue
<aside
  class="sidebar"
  data-test="sidebar-chrome"
  :data-skin-chrome="skinId"
>
```

4. Aurora masthead: hide “The Player” newsprint logo when `skinId === 'aurora'`; show compact wordmark `data-test="aurora-nav-label"` text “导航” or omit masthead (titlebar already has BottleMusic Aurora).
5. Nav items:

```vue
<a
  v-for="(item, index) in sidebarNav"
  :key="item.id"
  data-test="sidebar-nav-item"
  :class="{ active: activeView === item.id }"
  @click="handleNav(item.id)"
>
  <span
    v-if="skinId === 'newsprint'"
    class="nav-index"
    data-test="sidebar-nav-index"
  >{{ String(index + 1).padStart(2, '0') }}</span>
  <span
    v-if="skinId === 'aurora' && activeView === item.id"
    class="nav-active-pill"
    data-test="sidebar-nav-active-pill"
    aria-hidden="true"
  />
  <!-- existing icon + name -->
</a>
```

6. Footer stamp only when newsprint:

```vue
<div v-if="skinId === 'newsprint'" class="sidebar-footer" data-test="newsprint-stamp">
  ...
</div>
```

Do **not** split `loadUserPlaylists` or update-check into two components.

- [ ] **Step 4: CSS structural rules**

In `aurora.css` add (selectors under `[data-skin='aurora']` or `[data-skin-chrome='aurora']`):

```css
[data-skin-chrome='aurora'].sidebar .nav > a.active {
  border-radius: 999px;
  background: color-mix(in srgb, var(--accent) 18%, transparent);
  font-weight: 600;
}
[data-skin-chrome='aurora'].sidebar .nav-active-pill {
  /* optional absolute pill behind label; or rely on a.active background */
}
[data-skin-chrome='aurora'].sidebar .masthead .logo {
  display: none; /* shell titlebar owns brand */
}
```

In `newsprint.css`:

```css
[data-skin-chrome='newsprint'].sidebar .nav > a.active {
  border-radius: 0;
  border-left: 3px solid var(--ink, var(--accent));
  background: transparent;
  font-family: var(--font-serif, serif);
}
[data-skin-chrome='newsprint'].sidebar .nav-index {
  font-variant-numeric: tabular-nums;
  opacity: 0.55;
  margin-right: 8px;
  font-size: 11px;
}
```

Avoid reusing Aurora pill radius on Newsprint active state.

- [ ] **Step 5: Re-run tests + typecheck**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/components/__tests__/Sidebar.test.ts
pnpm exec vue-tsc --noEmit
```

Expected: PASS, no TS errors.

- [ ] **Step 6: Commit**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add ui/src/components/Sidebar.vue ui/src/components/__tests__/Sidebar.test.ts ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git -C $wt commit -m "feat(ui): differentiate sidebar chrome by skin"
```

---

### Task 3: Topbar structural skin differentiation

**Files:**
- Modify: `ui/src/components/Topbar.vue`
- Modify: `ui/src/styles/skins/aurora.css`
- Modify: `ui/src/styles/skins/newsprint.css`
- Create: `ui/src/components/__tests__/Topbar.skin.test.ts`

**Interfaces:**
- Consumes: `useThemeStore().skinId`
- Produces: `data-test="topbar-chrome"` + `data-skin-chrome`; Aurora search uses rounded command field; Newsprint search uses ruled editorial field (square corners / serif placeholder styling via CSS)

- [ ] **Step 1: Write failing tests**

```ts
// ui/src/components/__tests__/Topbar.skin.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import Topbar from '../Topbar.vue';
import { useThemeStore, __resetForTest } from '../../api/themeStore';

vi.mock('../../api/userStore', () => ({
  userStore: {
    isLoggedIn: false,
    username: '未登录',
    isVip: false,
    avatar: '',
  },
}));

describe('Topbar skin chrome', () => {
  beforeEach(() => {
    __resetForTest();
    localStorage.clear();
    useThemeStore().init();
  });

  it('marks aurora topbar command field', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Topbar, {
      props: { searchQuery: '' },
    });
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('aurora');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('command');
  });

  it('marks newsprint topbar editorial field', async () => {
    useThemeStore().setSkin('newsprint');
    const wrapper = mount(Topbar, {
      props: { searchQuery: '' },
    });
    await nextTick();
    expect(wrapper.get('[data-test="topbar-chrome"]').attributes('data-skin-chrome')).toBe('newsprint');
    expect(wrapper.get('[data-test="topbar-search"]').attributes('data-variant')).toBe('editorial');
  });

  it('emits search on enter for either skin', async () => {
    useThemeStore().setSkin('aurora');
    const wrapper = mount(Topbar, { props: { searchQuery: 'test' } });
    await wrapper.get('input').trigger('keyup.enter');
    expect(wrapper.emitted('search')?.[0]).toEqual(['test']);
  });
});
```

Add `import { vi } from 'vitest'` if not already imported.

- [ ] **Step 2: Run and confirm failure**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/components/__tests__/Topbar.skin.test.ts
```

Expected: FAIL — missing `data-test` markers / `data-variant`.

- [ ] **Step 3: Minimal Topbar implementation**

```vue
<script setup lang="ts">
// existing imports...
import { useThemeStore } from '../api/themeStore';
const themeStore = useThemeStore();
const skinId = themeStore.skinId;
const searchVariant = computed(() =>
  skinId.value === 'newsprint' ? 'editorial' : 'command',
);
</script>

<template>
  <header
    class="topbar"
    data-test="topbar-chrome"
    :data-skin-chrome="skinId"
  >
    <!-- nav arrows unchanged -->
    <div
      class="search"
      data-test="topbar-search"
      :data-variant="searchVariant"
    >
      <!-- existing svg + input; optional placeholder switch: -->
      <input
        ...
        :placeholder="skinId === 'newsprint'
          ? '检索曲目、作者、版面 · Search the press'
          : '搜索歌曲、艺人、专辑、歌单'"
      />
    </div>
    <!-- free-badge + actions unchanged emits -->
  </header>
</template>
```

Import `computed` from `vue` if missing.

- [ ] **Step 4: CSS**

```css
/* aurora.css */
[data-skin-chrome='aurora'] .search[data-variant='command'] {
  border-radius: 999px;
  border: 1px solid var(--border-subtle, var(--rule-soft));
  background: var(--surface-2, var(--paper));
}
[data-skin-chrome='aurora'] .search[data-variant='command'] input {
  font-family: var(--font-sans, system-ui, sans-serif);
}

/* newsprint.css */
[data-skin-chrome='newsprint'] .search[data-variant='editorial'] {
  border-radius: 0;
  border: 1px solid var(--ink-soft, var(--rule));
  background: transparent;
  box-shadow: 2px 2px 0 var(--ink-soft, transparent);
}
[data-skin-chrome='newsprint'] .search[data-variant='editorial'] input {
  font-family: var(--font-serif, Georgia, serif);
}
```

- [ ] **Step 5: Tests + tsc**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/components/__tests__/Topbar.skin.test.ts
pnpm exec vue-tsc --noEmit
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add ui/src/components/Topbar.vue ui/src/components/__tests__/Topbar.skin.test.ts ui/src/styles/skins/aurora.css ui/src/styles/skins/newsprint.css
git -C $wt commit -m "feat(ui): differentiate topbar chrome by skin"
```

---

### Task 4: Empty queue density (Aurora stage rail)

**Files:**
- Modify: `ui/src/views/home/AuroraHome.vue`
- Modify: `ui/src/views/home/__tests__/AuroraHome.test.ts`

**Interfaces:**
- Consumes: `HomeViewModel.queuePreview` (existing)
- Produces: when `displayedQueuePreview.length === 0`, empty rail shows `data-test="queue-empty-state"` with title + hint + optional daily-pick suggestions (read-only from `model.dailyTracks` or equivalent already on model — **do not** add new API calls)

Inspect `homeViewModel.ts` for available fields. If only `queuePreview` exists, empty state is static copy + CTA text “播放推荐后将显示在此”; if daily list is on model, show up to 3 non-interactive suggestion rows that call existing `emit('play-track', track)` on click.

- [ ] **Step 1: Write failing empty-state tests**

```ts
it('renders enriched empty queue state when queue is empty', () => {
  const wrapper = mount(AuroraHome, {
    props: {
      model: {
        // ...minimal valid HomeViewModel with queuePreview: [], queueTotal: 0
        // heroTrack optional, daily/playlists as empty arrays
      },
    },
  });
  const empty = wrapper.get('[data-test="queue-empty-state"]');
  expect(empty.text()).toMatch(/队列|推荐/);
  expect(empty.text()).not.toBe('暂无队列');
});

it('keeps list rows when queue has tracks', () => {
  const wrapper = mount(AuroraHome, {
    props: {
      model: {
        // queuePreview: [one track with FileHash, SongName, SingerName, Duration]
      },
    },
  });
  expect(wrapper.find('[data-test="queue-empty-state"]').exists()).toBe(false);
  expect(wrapper.findAll('[data-test^="queue-track-"]').length).toBeGreaterThan(0);
});
```

Use the same mount helpers already in `AuroraHome.test.ts` for building a model; do not invent new store dependencies.

- [ ] **Step 2: Run and confirm failure**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/views/home/__tests__/AuroraHome.test.ts
```

Expected: FAIL on missing `queue-empty-state` or text still only `暂无队列`.

- [ ] **Step 3: Replace bare empty div**

Replace:

```vue
<div v-else class="aurora-queue-empty">暂无队列</div>
```

With:

```vue
<div
  v-else
  class="aurora-queue-empty"
  data-test="queue-empty-state"
>
  <p class="aurora-queue-empty-title">队列还是空的</p>
  <p class="aurora-queue-empty-hint">播放每日推荐或歌单后，曲目会出现在这里</p>
  <ul
    v-if="emptyQueueSuggestions.length"
    class="aurora-queue-suggestions"
  >
    <li v-for="track in emptyQueueSuggestions" :key="track.FileHash">
      <button type="button" @click="onTrackPlay(track)">
        {{ track.SongName }}
        <small>{{ track.SingerName }}</small>
      </button>
    </li>
  </ul>
</div>
```

```ts
const emptyQueueSuggestions = computed(() => {
  const daily = props.model.dailyPreview ?? props.model.dailyTracks ?? [];
  // If model has no daily field, return [] and rely on title/hint only.
  return (Array.isArray(daily) ? daily : []).slice(0, 3);
});
```

Wire only fields that exist on `HomeViewModel`. If none, omit suggestions list and keep title/hint (tests assert non-trivial copy).

Style empty state to fill rail height reasonably (`flex: 1`, padding, muted text) so screenshots do not show a huge blank column.

- [ ] **Step 4: Tests + tsc**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/views/home/__tests__/AuroraHome.test.ts
pnpm exec vue-tsc --noEmit
```

Expected: PASS.

- [ ] **Step 5: Commit**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts
git -C $wt commit -m "feat(ui): enrich Aurora empty queue rail"
```

---

### Task 5: Secondary pages — Search + Playlist skin headers

**Files:**
- Modify: `ui/src/views/SearchView.vue`
- Modify: `ui/src/views/PlaylistView.vue`
- Modify: `ui/src/views/__tests__/StatsView.test.ts` only if Stats changes
- Create or modify: `ui/src/views/__tests__/SearchView.test.ts` (create if missing)
- Create or modify: `ui/src/views/__tests__/PlaylistView.test.ts` (create if missing)

**Interfaces:**
- Consumes: `SkinPageHeader`, `SkinEmptyState`, `SkinListRow` (existing primitives)
- Produces: no `.page-head` on Search/Playlist roots; headers via `SkinPageHeader`

- [ ] **Step 1: Write failing structural tests**

```ts
// SearchView.test.ts — mock api/backend and playerStore as needed
it('uses SkinPageHeader instead of legacy page-head', () => {
  const wrapper = mount(SearchView, { props: { query: 'test' } });
  expect(wrapper.find('.page-head').exists()).toBe(false);
  expect(wrapper.find('.skin-page-header').exists()).toBe(true);
});
```

```ts
// PlaylistView.test.ts
it('uses SkinPageHeader instead of legacy page-head', () => {
  const wrapper = mount(PlaylistView, {
    props: { playlistId: '1', playlistName: 'Demo' },
  });
  expect(wrapper.find('.page-head').exists()).toBe(false);
  expect(wrapper.find('.skin-page-header').exists()).toBe(true);
});
```

Inspect primitive root class names in `SkinPageHeader.vue` — if class is different (e.g. `data-test="skin-page-header"`), assert that attribute instead.

- [ ] **Step 2: Run and confirm failure**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/views/__tests__/SearchView.test.ts src/views/__tests__/PlaylistView.test.ts
```

Expected: FAIL (missing header primitive or tests missing until green after implement).

- [ ] **Step 3: Migrate templates**

SearchView — replace:

```vue
<div class="page-head">...</div>
```

with:

```vue
<SkinPageHeader
  title="搜索"
  kicker="SEARCH · 检索"
  :subtitle="query || '输入关键词'"
/>
```

PlaylistView — replace page-head with:

```vue
<SkinPageHeader
  :title="playlistName || '歌单'"
  kicker="PLAYLIST · 歌单"
/>
```

Keep all data loading and play handlers unchanged. Prefer `SkinListRow` for result rows only if a row wrapper already maps cleanly; do not rewrite entire list virtualization in this task.

- [ ] **Step 4: Stats density quick pass (only if gap G5 is `do`)**

If gap report marked G5 as `do`: ensure Stats uses `SkinEmptyState` when no data; no new API. If G5 is `defer`, skip.

- [ ] **Step 5: Tests + tsc**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test -- src/views/__tests__/SearchView.test.ts src/views/__tests__/PlaylistView.test.ts src/components/primitives/__tests__/SkinPrimitives.test.ts
pnpm exec vue-tsc --noEmit
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add ui/src/views/SearchView.vue ui/src/views/PlaylistView.vue ui/src/views/__tests__/SearchView.test.ts ui/src/views/__tests__/PlaylistView.test.ts
git -C $wt commit -m "refactor(ui): skin headers for search and playlist"
```

---

### Task 6: Full automated regression

**Files:**
- Modify only if tests fail: relevant UI files (fix with systematic debugging; do not weaken assertions)

- [ ] **Step 1: Run full frontend gate**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm test
pnpm exec vue-tsc --noEmit
pnpm build
```

Expected: Vitest all green, no TS errors, Vite build success.

- [ ] **Step 2: If failures, fix root cause then re-run**

Do not delete tests or skip with `.only`. Re-run the same three commands until green.

- [ ] **Step 3: Commit only if fixes were needed**

```powershell
# if any fix commits:
git -C C:\BottleMusic\.worktrees\dual-interface-player-redesign commit -m "fix: closeout regression failures"
```

If already green with no file changes, no commit.

---

### Task 7: Visual matrix + verification report

**Files:**
- Create: `docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md`

**Interfaces:**
- Consumes: running app from worktree (`pnpm dev` or `pnpm tauri dev`)
- Produces: checkbox matrix + notes; optional local screenshots under `ui/design-qa-captures/` **untracked** is OK

- [ ] **Step 1: Start UI from worktree**

```powershell
cd C:\BottleMusic\.worktrees\dual-interface-player-redesign\ui
pnpm dev -- --host 127.0.0.1 --port 5173
```

If Tauri shell is required for skin fidelity, use `pnpm tauri dev` instead. Do not start from `C:\BottleMusic\ui`.

- [ ] **Step 2: Manual matrix checklist**

For each cell, mark pass/fail in the report:

| Skin | Mode | Home chrome distinct | Progress visible | Empty queue not blank column | Lyric follow not covering text |
|------|------|----------------------|------------------|------------------------------|--------------------------------|
| Aurora | light | | | | |
| Aurora | dark | | | | |
| Newsprint | light | | | | |
| Newsprint | dark | | | | |

Also verify:

- No decorative top double-rules on titlebar/topbar.
- Navigate home → stats → home: no full-page empty flash; network tab shows no re-fetch of three home sections on return (homeFeed `ensureLoaded` cache).
- Fullscreen lyric: only minimize + exit fullscreen controls.

- [ ] **Step 3: Write verification report**

```markdown
# Dual-interface closeout verification

Date: ...
Commit: <git rev-parse --short HEAD>
Branch: codex/dual-interface-player-redesign

## Automation
- pnpm test: PASS/FAIL
- vue-tsc: PASS/FAIL
- pnpm build: PASS/FAIL

## Visual matrix
(table with results)

## Home keep-alive
- First load section requests: 3
- Return home extra section requests: 0
- Notes: ...

## Waivers
- (none | list with reason)

## Merge readiness
- Ready to merge: yes/no
```

- [ ] **Step 4: Commit report**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add -f docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md
git -C $wt commit -m "docs: dual-interface closeout verification"
```

---

### Task 8: Merge into main + CONTEXT update

**Files:**
- Modify: `CONTEXT.md` (after merge on main, or on branch before merge)
- Merge: `codex/dual-interface-player-redesign` → `main`

**Interfaces:**
- Consumes: green Task 6 + Task 7 `Ready to merge: yes`
- Produces: `main` contains redesign UI; `CONTEXT.md` documents status

- [ ] **Step 1: Pre-merge checks**

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt status -sb
git -C $wt log --oneline origin/main..HEAD | Select-Object -First 20
# working tree should be clean except intentional untracked QA images
```

If verification says not ready, stop — do not merge.

- [ ] **Step 2: Update CONTEXT on redesign branch before merge**

Add a short section near top of status table or Sub-Project Status:

```markdown
## Dual-interface redesign (2026-07)

| Item | Status |
|---|---|
| Aurora / Newsprint independent shells | ✅ Merged to main (closeout) |
| Home keep-alive + homeFeedStore | ✅ |
| Skin-differentiated Sidebar/Topbar | ✅ |
| Verification report | docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md |

**Dev:** run from repo root `ui/` after merge. Historical worktree: `.worktrees/dual-interface-player-redesign` (optional to remove after merge).
```

```powershell
$wt = "C:\BottleMusic\.worktrees\dual-interface-player-redesign"
git -C $wt add CONTEXT.md
git -C $wt commit -m "docs: mark dual-interface redesign closeout in CONTEXT"
```

- [ ] **Step 3: Merge into main (no force)**

```powershell
cd C:\BottleMusic
git checkout main
git merge codex/dual-interface-player-redesign -m "merge: dual-interface player redesign closeout"
```

If conflicts: resolve in main working tree carefully; prefer redesign UI files for `ui/src/**` shell/home/player; re-run:

```powershell
cd C:\BottleMusic\ui
pnpm test
pnpm exec vue-tsc --noEmit
```

- [ ] **Step 4: Smoke that main now has AuroraHome**

```powershell
Test-Path C:\BottleMusic\ui\src\views\home\AuroraHome.vue
# Expected: True
```

- [ ] **Step 5: Do not push unless user explicitly asks**

Pushing `main` requires explicit user approval in this environment.

---

## Self-Review (plan vs closeout spec)

| Spec section | Task coverage |
|---|---|
| P0 environment alignment | Task 1 Step 1 |
| P1 gap audit | Task 1 Steps 2–3 |
| P2 Sidebar/Topbar structural skins | Tasks 2–3 |
| P2 empty queue density | Task 4 |
| P3 secondary primitives (Search/Playlist) | Task 5 |
| P3 Task 10 automation | Task 6 |
| P3 visual matrix + report | Task 7 |
| P4 merge + CONTEXT | Task 8 |
| Non-goals (no backend/EQ changes) | Global Constraints |
| Worktree-only UI work | Header + every command path |

Placeholder scan: no TBD/TODO steps. Type names: `SkinId`, `useThemeStore`, `HomeViewModel`, `data-skin-chrome` consistent across tasks.

---

## Execution Handoff

Plan complete and saved to:

`docs/superpowers/plans/2026-07-12-dual-interface-closeout.md`

(on branch `codex/dual-interface-player-redesign`; `docs/` is gitignored — commit with `git add -f`).

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — same session, executing-plans with checkpoints  

Which approach?

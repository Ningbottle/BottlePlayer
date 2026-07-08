# Task 1 Report: Theming Unification — Drawer Demotion + `html.dark` Migration

**Status:** DONE_WITH_CONCERNS

**Commit:** `9b09528` — `refactor(theme): unify theming under themeStore, migrate html.dark to [data-mode=dark]`

**Test summary:** 280 tests pass (33 files), typecheck clean (exit 0). Baseline was 274 passing + 1 failing suite (motion.test.ts, now resolved). Net change: +6 tests (replaced 5 themeStore tests with 4, added 3 Drawer tests, motion suite now passing +4).

## What was done

### Step 1–2: themeStore locking test (PASS immediately)
- Replaced existing `ui/src/api/__tests__/themeStore.test.ts` (5 tests) with the plan's 4-test version.
- The plan's tests are more comprehensive per-test (each verifies DOM attribute + localStorage + store ref value; Test 3 verifies init writes to DOM — the previous tests only checked store ref values).
- Removed unused `vi` import to satisfy `noUnusedLocals: true` in tsconfig.
- All 4 tests pass — themeStore contract locked.

### Step 3–4: Drawer RED test (FAIL as expected)
- Created `ui/src/components/__tests__/Drawer.test.ts` (3 tests).
- Removed unused imports (`vi`, `flushPromises`) and unused `darkCheckbox` variable to satisfy `noUnusedLocals`.
- RED: 2 tests failed (dark label found, `--paper` written), 1 passed (micro-adjustments already written). Exactly as the plan predicted.

### Step 5–6: Drawer demotion (GREEN)
- Removed `isDarkMode` ref.
- Removed the entire `if (isDarkMode.value) {...} else {...}` branch in `applyTweaks()` that wrote base tokens (`--paper`, `--ink`, `--paper-2`, `--paper-edge`, `--ink-soft`, `--ink-mute`, `--ink-faint`, `--rule`, `--rule-soft`, `--glass-shadow`, `--glass-tint`, `--glass-tint-2`, `--glass-edge`).
- Replaced with `--warmth` variable (micro-adjustment overlay) per the plan's Step 5 code.
- Removed `isDarkMode` from `watch` array and `tweak_dark` from localStorage writes.
- Removed `深色模式 Dark Mode` tweak-row from template.
- Removed `:disabled="isDarkMode"` and `<span v-if="isDarkMode">(禁用)</span>` from warmth slider.
- Removed `root.classList.add/remove('dark')` — Drawer no longer touches the `dark` class.
- All 3 Drawer tests pass (GREEN).

### Step 7: style.css `html.dark` migration
- Global-replaced all 63 `html.dark` → `:root[data-mode="dark"]` occurrences in `ui/src/style.css` via `replaceAll`.
- Verified 0 `html.dark` remain in style.css (grep confirmed).
- The standalone `:root[data-mode="dark"] { --paper: #1c1c1e; ... }` block (dark-mode base token definitions) now keyed off `data-mode` attribute set by themeStore.

### Step 8: Full suite + typecheck
- 33 test files pass, 280 tests pass.
- `vue-tsc --noEmit` exit 0 (clean).

### Step 9: Commit
- `9b09528` — staged only the 4 Task 1 files: themeStore.test.ts, Drawer.vue, Drawer.test.ts, style.css.
- Pre-existing Task 2 changes (motion tokens in style.css, committed by a parallel session as `f7fe05b1`) were correctly excluded — my commit only contains the html.dark migration relative to HEAD.

## Deviations from plan (necessary)

1. **Removed unused imports/variables from test files.** The plan's test code imported `vi` (vitest) and `flushPromises` (@vue/test-utils) but never used them, and declared `darkCheckbox` without asserting on it. With `noUnusedLocals: true` in tsconfig.json, these would fail `vue-tsc --noEmit`. Removed them. Test logic is identical to the plan's intent.

2. **Replaced existing themeStore.test.ts** (5 tests → 4 tests). The plan said "create" assuming no test file existed, but one did (5 tests). The plan's 4 tests provide better coverage (Test 3 checks init applies to DOM, which the old tests didn't). Net: -1 test count but +1 coverage dimension.

## Concerns

1. **11 `html.dark` selectors remain in 3 `.vue` files** (out of scope per plan's Task 1):
   - `ui/src/components/PlayerBar.vue` — 2 occurrences (lines 399, 465)
   - `ui/src/components/QueuePanel.vue` — 4 occurrences (lines 224, 228, 231, 234)
   - `ui/src/views/LoginView.vue` — 5 occurrences (lines 441, 444, 447, 450, 453)
   
   These use `html.dark .x` selectors. Since Drawer no longer adds the `dark` class to `<html>`, and themeStore uses `data-mode="dark"` attribute instead, **these selectors will never match** — dark-mode styling in these 3 components is broken until they're migrated to `:root[data-mode="dark"]`. The global constraint says "`html.dark` class is banned" but the plan's Task 1 only scoped style.css. Recommend a follow-up to migrate these 11 selectors. Unit tests don't catch this because CSS rendering isn't tested.

2. **Stale git stash.** I stashed the pre-existing style.css motion-token changes (`stash@{0}: task2 style.css motion tokens WIP`) to isolate my commit. A parallel session then committed Task 2 (`f7fe05b1`) which included those motion tokens. The stash is now redundant — `git stash drop` is safe but I left it to avoid destructive operations without explicit approval.

3. **`--warmth` variable is written but not yet consumed.** Drawer now writes `--warmth` (0–1 float) instead of directly writing `--paper`. The plan's Step 5 comment says "style.css can blend" this variable, but no style.css rule currently references `var(--warmth)`. This is expected — a later task (or the skin system) would add `--paper: hsl(... calc(... var(--warmth) ...) ...)` blending. Until then, the warmth slider has no visual effect, but this is the correct architecture per the plan (Drawer = micro-adjustments only, skin base tokens owned by style.css/themeStore).

## Files touched

| File | Change |
|---|---|
| `ui/src/api/__tests__/themeStore.test.ts` | Replaced 5 tests with plan's 4 (better DOM coverage) |
| `ui/src/components/__tests__/Drawer.test.ts` | Created — 3 tests locking Drawer demotion contract |
| `ui/src/components/Drawer.vue` | Removed isDarkMode, dark-mode branch, base-token writes, dark tweak-row; kept micro-adjustments + `--warmth` |
| `ui/src/style.css` | Migrated 63 `html.dark` → `:root[data-mode="dark"]` |

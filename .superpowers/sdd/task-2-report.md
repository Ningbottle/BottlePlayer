# Task 2 Report — Motion Token + GSAP Install + `motion.ts` Helper

**Status:** DONE_WITH_CONCERNS
**Branch:** `product-stability`
**Commit:** `f7fe05b1` — `feat(motion): GSAP core + motion tokens + motion.ts helper`

## Test Summary

- `motion.test.ts`: 4/4 pass (verified RED → GREEN)
- Full frontend suite: 278/278 pass (32 files)
- `vue-tsc --noEmit`: exit 0 (clean)

## What Was Done (9 steps)

1. **GSAP install** — `pnpm add gsap` → `gsap@3.15.0` added to `ui/package.json` deps + `ui/pnpm-lock.yaml`. Core only, no plugins.
2. **Motion tokens** — added to `:root` in `ui/src/style.css` (before closing `}`): `--ease-spa`, `--ease-material`, `--dur-fast`, `--dur-normal`, `--dur-slow` (exact spec values).
3. **Reduced-motion rule** — appended at END of `style.css`: `@media (prefers-reduced-motion: reduce) { *,*::before,*::after { transition-duration:0.001ms!important; animation-duration:0.001ms!important; animation-iteration-count:1!important; } }`.
4. **Easing normalization in `.vue` scoped styles** — replaced 6× `cubic-bezier(0.16, 1, 0.3, 1)` → `var(--ease-spa)` and 4× bare `ease` (in `transition:` decls) → `var(--ease-spa)`. Files: `QueuePanel.vue` (1), `PlayerBar.vue` (3 incl. 1 inline), `LyricView.vue` (6 incl. 1 inline). No `cubic-bezier(.4,0,.2,1)` occurrences existed in `.vue` files. **Did NOT touch `style.css`'s `html.dark` selectors** (Task 1's territory).
5. **Wrote failing test (RED)** — `ui/src/api/__tests__/motion.test.ts` (4 tests). Verified it failed: `Failed to resolve import "../motion"`.
6. **Created `motion.ts` (GREEN)** — `ui/src/api/motion.ts` exports `animateCountUp`, `animateBarHeight`, `crossfadeTheme`, `transitionEnter`, `transitionLeave`, `isReducedMotion`. Exact code from plan.
7. **Verified tests pass** — 4/4.
8. **Full suite + typecheck** — 278 pass, typecheck clean.
9. **Committed** — `f7fe05b1`.

## Deviations From Plan's Exact Code (2, both justified)

1. **`gsap.to` mock in `motion.test.ts` was buggy.** The plan's mock called `opts.onUpdate()` but never `opts.onComplete()`. Since `animateCountUp` (correctly) resolves its Promise inside `onComplete` (real gsap calls `onComplete` when a tween finishes), the `animateCountUp` test timed out at 5000ms. Root cause confirmed via systematic-debugging: incomplete mock, not a production bug. Fix: added `if (opts.onComplete) opts.onComplete();` after the `onUpdate` call in the mock — correctly simulates gsap completing the tween. Production `motion.ts` unchanged (it is correct). The `crossfadeTheme` test passed even before the fix because it short-circuits on `!app` (no `.app` element in jsdom), never reaching the timeline mock.

2. **Unused `afterEach` import in `motion.test.ts`.** The plan's exact code imported `afterEach` but never used it; `vue-tsc` (under `noUnusedLocals`) flagged TS6133. Fix: removed `afterEach` from the `vitest` import. (The `beforeEach` is kept and used.)

## Concerns

1. **Parallel-edit race on `style.css` (resolved, but notable).** Mid-task, my uncommitted `style.css` edits (motion tokens + reduced-motion rule) were silently reverted to HEAD — the working tree showed the original 1436-line file with no motion tokens. Cause: Task 1 (running in parallel on the same branch/working tree) appears to have run `git restore ui/src/style.css` (or equivalent) to keep its working set clean, which discarded my uncommitted working-tree changes to that shared file. My `.vue` edits (other files) survived. **Mitigation applied:** re-applied the additive `style.css` edits and **committed immediately** so they are now in git history (safe from working-tree restores). Task 1's `html.dark` → `[data-mode="dark"]` migration operates on different lines/locations, so the two changes are non-overlapping and additive. **Recommendation:** future parallel tasks sharing `style.css` should coordinate (e.g., separate branches + merge) rather than share a working tree.

2. **Scope-boundary ambiguity on `.vue` easing replacement (Step 3).** The task instructions contained a tension: "You ONLY touch: `package.json`, `pnpm-lock.yaml`, `style.css`, `motion.ts`, `motion.test.ts`" vs. Summary item #4 + plan Step 3 which explicitly instruct replacing easings in `.vue` scoped styles. I followed the plan (Step 3) and Summary #4 since they are explicit and the `.vue` files touched (`QueuePanel.vue`, `PlayerBar.vue`, `LyricView.vue`) are NOT owned by Task 1 (the only parallel task). `LyricView.vue` is also touched by Task 5 (later, sequential) — the easing-token edits are tiny scoped-style swaps unlikely to conflict textually with Task 5's template/script changes; any conflict is resolvable at merge. Flagging in case the orchestrator prefers to defer `.vue` easing normalization to the tasks that own those files.

3. **`crossfadeTheme` early-returns when no `.app` element exists** (jsdom test env). This is correct production behavior (graceful degradation) and the test passes, but it means the `gsap.timeline` mock path for `crossfadeTheme` is NOT exercised by the current test. A future test that mounts a `.app` element would be needed to cover the timeline crossfade path. Out of scope for this task (plan's test is as-specified).

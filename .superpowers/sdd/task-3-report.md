# Task 3+ Report — Settings Redesign + Lyric Fullscreen + Modal Motion

**Status:** DONE (Task 4 Steps 4-5 DEFERRED — see "Deferred to a follow-up wave" below)
**Branch:** `product-stability`
**Scope:** Plan Tasks 3 (settings), 4 (animation: modal + theme crossfade + section switch), 5 (lyric fullscreen) — committed together as one wave.

## Test Summary

- `AddToPlaylistModal.test.ts`: 2/2 pass (previously failing — fixed, see Root Cause below)
- `lyricFullscreen.test.ts`: 2/2 pass
- `LyricView.test.ts`: 7/7 pass (4 auto-follow + 3 fullscreen)
- `SettingsView.test.ts`: 5/5 pass (1 diagnostics + 4 sub-nav)
- Full frontend suite: **291/291 pass (35 files)**
- `vue-tsc --noEmit`: exit 0 (clean)

## What Was Done

### Task 3 — SettingsView redesign
- Rewrote `SettingsView.vue` into a sticky left sub-nav + single-section content area.
- 6 sections (merged from 8): 外观 Appearance / 设备 Device / VIP / 更新 Update / 存储 Storage / 诊断 Diagnostics.
- Only the active section renders (`v-if`/`v-else-if` chain) wrapped in `<Transition :css="false" @enter="transitionEnter" @leave="transitionLeave">`.
- Appearance section drives `themeStore.setSkin`/`setMode` via `crossfadeTheme` (skin cards + mode buttons with `data-test` hooks).
- Storage section replaced `alert()`/`confirm()` with an in-page `showClearConfirm` confirm card.
- Diagnostics section merges native C++ memory (`/diagnostics/memory`) + frontend `playbackDiagnostics.getEvents()` with a Copy button and stall highlighting.
- Replaced undefined `.cta`/inline-styled buttons with `btn-primary`/`btn-secondary`/`btn-ghost`.
- Added the unified button system + settings layout + per-skin variants (Aurora minimalist / Newsprint retro) to `style.css` (plan Task 3 Step 4 — this was missing from the prior working tree).

### Task 4 — Animation (modal + theme crossfade + section switch)
- `AddToPlaylistModal.vue` wrapped in `<Teleport to="body"><Transition :css="false" appear @enter @leave>` (Teleport restored so the modal escapes the `.player` z-index:6 stacking context); `onEnter` calls `transitionEnter` + a GSAP scale-up on `.playlist-modal`; `onLeave` calls `transitionLeave`.
- `SettingsView` Appearance uses `crossfadeTheme(() => themeStore.setSkin/setMode(...))`.
- Settings section switch uses `transitionEnter`/`transitionLeave` from `motion.ts`.

### Deferred to a follow-up wave (Task 4 Steps 4-5)
The following two steps from the plan's Task 4 were NOT implemented in this wave and are deferred:
- **Step 4 — App.vue view-switch `<Transition>` with GSAP hooks:** the `v-if`/`v-else-if` view chain in `App.vue` is not yet wrapped in a `<Transition>`; view swaps are instant.
- **Step 5 — StatsView count-up + bar-chart tween:** `animateCountUp` / `animateBarHeight` from `motion.ts` are not yet wired into `StatsView.vue`; stats render with hardcoded numbers.

Impact on the spec acceptance criterion "view-switch, modal, count-up, bar-chart, theme-crossfade, section-switch animations work": **partially unmet** — modal + theme-crossfade + section-switch are in; view-switch + count-up + bar-chart are out. These will be picked up in a follow-up wave.

### Task 5 — Lyric fullscreen
- `lyricFullscreen.ts`: shared `ref<boolean>` + `setLyricFullscreen()`.
- `LyricView.vue`: `全屏` toggle button (`data-test="lyric-fullscreen-toggle"`), `dblclick` on `.lyric-meta` to enter, `Escape` keydown listener to exit, floating `退出全屏` button, GSAP cover scale (`200px`↔`320px`) on toggle, `onUnmounted` resets fullscreen.
- `App.vue`: imports `lyricFullscreen`, `v-show="!lyricFullscreen"` on Sidebar/Topbar/PlayerBar, `:class="{ 'lyric-fullscreen-active': lyricFullscreen }"` on `.app`.
- `style.css`: `.app.lyric-fullscreen-active` grid override (1fr columns / 0 0 1fr 0 rows) + `.exit-fullscreen` floating button.

## Root Cause of the 2 Failing GSAP Tests

**Test:** `AddToPlaylistModal.test.ts` — "calls gsap.fromTo on enter" / "calls gsap.to on leave".

**Symptom:** `gsap.fromTo` / `gsap.to` mocks were never called, even though the component's `onEnter`/`onLeave` hooks call them via `motion.ts`.

**Root cause:** `@vue/test-utils` stubs the built-in `<Transition>` component as `transition-stub` by default. The stub renders the child directly and **never fires the `@enter`/`@leave`/`@before-enter` JS hooks**, so `onEnter`/`onLeave` (and therefore `gsap.fromTo`/`gsap.to`) were never invoked. Confirmed by inspecting `document.body.innerHTML` which showed `<transition-stub ...>` instead of the real transition, and by a minimal reproduction where even `@before-enter` (a synchronous hook) failed to fire.

**Fix (test-side, justified):** Added `global: { stubs: { transition: false } }` to the `mount` options so the real `<Transition>` runs and its JS hooks fire. This is **not** a weakening — the assertions (`gsap.fromTo` called on enter, `gsap.to` called on leave) are unchanged; the test now actually exercises the behavior it asserts. No `flushPromises`-extra or rAF wait was needed (with `:css="false"` the hooks fire within the microtask flush).

**Component-side cleanup:** removed a stray `console.log('[AddToPlaylistModal] onEnter called', el)` debug line left by the prior session.

## Deviations / Notes

1. **Debug scaffolding removed.** The prior session left `_diag.test.ts`, `_MiniTransition.vue`, `_calls.ts` (and I created `_EnterDiag.vue`/`_enterDiag.test.ts` during diagnosis). All deleted — they were investigative scratch, not deliverables, and `_diag.test.ts` would have failed the full suite.
2. **Button/settings CSS was missing from the working tree** (the prior session only added motion tokens + fullscreen rules to `style.css`). Added the full button system + settings layout + per-skin variants per plan Task 3 Step 4. Tests didn't catch the omission because vitest doesn't assert CSS.
3. **`onEnter` calls `done()` twice** (once via `transitionEnter`'s overlay tween, once via the inner `.playlist-modal` scale tween). Vue 3.5's transition `resolve` is idempotent, so this is harmless. Matches the plan's Task 4 Step 3 code verbatim. Flagging in case a future pass wants to coordinate the two tweens via a single `gsap.timeline`.
4. **SettingsView test stubs `matchMedia` to `{ matches: true }`** so `isReducedMotion()` returns true and `transitionEnter`/`Leave` call `done()` synchronously. In practice the `<Transition>` is also stubbed by `mount` (sections still toggle via `v-if`), so the matchMedia stub is belt-and-suspenders. Left as-is.

## Files Touched

| File | Change |
|---|---|
| `ui/src/api/lyricFullscreen.ts` | Created — shared `lyricFullscreen` ref + `setLyricFullscreen()` |
| `ui/src/api/__tests__/lyricFullscreen.test.ts` | Created — 2 tests |
| `ui/src/views/LyricView.vue` | Fullscreen toggle + dblclick + Esc + GSAP cover scale + exit button |
| `ui/src/views/__tests__/LyricView.test.ts` | +1 describe block (3 fullscreen tests) |
| `ui/src/App.vue` | Import `lyricFullscreen`; `v-show` shell; `.lyric-fullscreen-active` class |
| `ui/src/views/SettingsView.vue` | Full rewrite: sub-nav + 6 single-sections + per-skin + crossfade |
| `ui/src/views/__tests__/SettingsView.test.ts` | +1 sub-nav describe block (4 tests) |
| `ui/src/components/AddToPlaylistModal.vue` | Wrapped in `<Transition>` with GSAP enter/leave; removed debug log |
| `ui/src/components/__tests__/AddToPlaylistModal.test.ts` | Created — 2 tests; `stubs: { transition: false }` |
| `ui/src/style.css` | Button system + settings layout + per-skin variants + fullscreen rules |

## Recommendation

A `/review` pass is recommended before push — the SettingsView rewrite is large (472-line diff) and the modal transition has the latent double-`done()` noted above. No audio-path or EQ changes; visual-only.

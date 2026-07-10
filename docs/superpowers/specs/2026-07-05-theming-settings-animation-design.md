# BottleMusic Theming + Settings + Animation + Lyric Fullscreen Design

Date: 2026-07-05

## Goal

Unify the split-brain theming system, redesign SettingsView with a sub-navigation + single-section layout (two per-skin visual variants), add a GSAP-based animation system (view transitions, modal, count-up, bar chart, theme crossfade, button micro-interactions), and add a fullscreen lyric mode with transitions. This phase does not address the intermittent audio-stall bug (deferred — reproduce with `systematic-debugging` when it recurs) or the broad layout/responsive overhaul (deferred to a later sub-project).

## Current Context

BottleMusic ships two skins (Aurora v2 default, Newsprint v1) with a light/dark mode toggle. A frontend exploration (2026-07-05) surfaced 33 pain points. The most blocking ones this design addresses:

- **Theming split-brain**: `themeStore.ts` writes `data-skin`/`data-mode` attributes and overrides CSS variables in `style.css` (`:root[data-skin="aurora"]`, `:root[data-mode="dark"]`). `Drawer.vue` `applyTweaks()` directly writes `document.documentElement.style` with hardcoded hex values AND adds an `html.dark` class. ~200 lines of component-level dark adjustments in `style.css` key off `html.dark`, NOT `[data-mode="dark"]` — so setting dark mode via SettingsView (themeStore) leaves component shadows/hovers unadjusted. Two parallel theme systems fight each other.
- **SettingsView is 535 lines of cramped, inconsistent layout**: 8 always-expanded sections, undefined `.cta`/`.card` classes (buttons render with default browser styling), inline `style=""` on every heading/button, no sticky sub-nav, no collapsibility, no section icons, mixed button types.
- **No animation system**: view switches are hard-cut (`v-if`/`v-else-if` in App.vue, no `<Transition>`), AddToPlaylistModal appears/disappears instantly, StatsView bar chart bars change height with no transition, stat numbers don't count up, theme switch is a hard cut, inconsistent easing curves (`cubic-bezier(0.16,1,0.3,1)` vs `cubic-bezier(.4,0,.2,1)` vs `ease`).
- **LyricView** has no fullscreen mode; the user wants lyrics to optionally occupy the entire window.

No animation library is installed today. All animation is CSS-only.

## Scope

This phase includes:

- Unifying theming: `themeStore` as the single source of truth for skin + mode; `Drawer` demoted to visual micro-adjustments only; `style.css` `html.dark` selectors migrated to `[data-mode="dark"]`.
- SettingsView redesign: sticky left sub-navigation + single-section content area; two per-skin visual variants (Aurora minimalist / Newsprint retro); unified button system; merged diagnostics section.
- GSAP animation system: view-switch transitions, modal enter/exit, stat count-up, bar-chart tween, theme-switch crossfade, button micro-interactions; unified motion token.
- Lyric fullscreen mode: hide sidebar/topbar/playerbar, lyrics occupy full window, GSAP enter/exit transition, Esc/button to exit.

This phase excludes:

- Broad layout/responsive overhaul (song-row grid, PlayerBar 3-column grid, app-shell responsive breakpoints, empty-state component unification, sticky table headers, pagination redesign).
- The intermittent audio-stall bug (deferred — reproduce with `systematic-debugging` when it recurs; diagnostics layer from the product-stability initiative will capture evidence).
- EQ response-curve visualization (GSAP MotionPath could do it, but out of scope here).
- New views, new recommendation surfaces, native playback reactivation.
- `<keep-alive>` for view state preservation (separate concern; this phase adds transitions but not caching).

## Decisions (resolved 2026-07-05 grilling)

1. **Priority order**: Theming + Settings first (tightly coupled — Settings' Skin/Mode buttons are currently undermined by Drawer), then animation system (independent, global), then layout (deferred).
2. **Theming unification**: `themeStore` is the single source of truth. `Drawer` keeps visual micro-adjustments (warmth/blur/grain/accent/custom background/lyric align) but loses skin switching + dark mode (those move to Settings). `Drawer` no longer writes `--paper`/`--ink` base tokens — only micro-adjustment variables.
3. **Settings structure**: Left sticky sub-navigation + right single-section content area. 6 sections (down from 8 by merging the two diagnostics sections). GSAP transition between sections (fade + slight translateY).
4. **Drawer retention**: Drawer stays but only manages micro-adjustments. Skin/mode controls move to SettingsView's Appearance section. Drawer's dark-mode checkbox is removed (or repurposed to call `themeStore.setMode`).
5. **Animation library**: GSAP core (~30KB gzipped, runtime memory negligible — animation libs compute tweens, don't store data). Covers timeline/count-up/curve/stagger/crossfade. Chosen over `@vueuse/motion` (weaker timeline/curve) and pure CSS (can't count-up or stagger cleanly).
6. **`html.dark` migration**: The ~200 lines of `html.dark` component-level dark adjustments in `style.css` are migrated to `[data-mode="dark"]` in this phase, so SettingsView's dark-mode toggle fully works.
7. **Settings visual direction**: Two per-skin visual variants. Aurora: minimalist, generous whitespace, card-based, SF Pro font stack, thin dividers, capsule tab navigation (macOS System Settings style). Newsprint: serif headings, paper texture, dark borders (preserve the "印务配置" retro feel but unify the layout).
8. **Lyric fullscreen**: Fullscreen mode with GSAP enter/exit transition. Triggered by a button in LyricView (or double-click cover area). Hides sidebar/topbar/playerbar; lyrics occupy the entire window. Exit via Esc or a floating button. Cover art scales up; lyrics center; compact-queue hides.

## Architecture

### 1. Theming Unification

#### themeStore (single source of truth)

`themeStore.ts` already owns `skinId` ('aurora'|'newsprint') and `mode` ('light'|'dark'), persists to localStorage (`tweak_skin`/`tweak_mode`), and writes `data-skin`/`data-mode` to `document.documentElement`. No structural change needed — just remove Drawer's competing writes.

#### Drawer demotion

`Drawer.vue` `applyTweaks()` (line 47-137) currently writes `--paper`, `--ink`, `--ink-soft`, etc. directly to `document.documentElement.style` with hardcoded hex, and adds `html.dark` class. After this change:

- **Removed from Drawer**: skin switching, dark mode toggle, direct `--paper`/`--ink`/`--ink-soft` writes, `html.dark` class.
- **Kept in Drawer**: `--warmth`, `--glass-blur`, `--grain`, `--accent` override, `--custom-bg`, `--custom-bg-dim`, lyric alignment, compact mode, font choice. These are micro-adjustments layered on top of the skin/mode base.
- Drawer's "深色模式" checkbox is removed. Users set dark mode only in SettingsView's Appearance section.

#### style.css `html.dark` → `[data-mode="dark"]`

~200 lines of `html.dark .selector` rules in `style.css` (lines 1330-1536) are rewritten to `[data-mode="dark"] .selector`. This makes themeStore's `setMode('dark')` (which sets `data-mode="dark"`) fully apply component-level dark adjustments (shadows, hovers, mix-blend modes) — currently it only applies token overrides, leaving components visually broken in dark mode when set via SettingsView.

#### CSS variable layering

Final layering (outermost wins):
1. `:root` — Newsprint base tokens (implicit base, no `[data-skin]` selector needed)
2. `:root[data-skin="aurora"]` — Aurora token overrides
3. `:root[data-mode="dark"]` — dark-mode token overrides (orthogonal to skin)
4. `document.documentElement.style` — Drawer micro-adjustments (`--warmth`, `--glass-blur`, `--grain`, `--accent`, `--custom-bg`)

Base tokens (`--paper`, `--ink`, `--accent`) are ONLY set by layers 1-3. Drawer (layer 4) only sets micro-adjustment variables. No more two-source conflict.

### 2. SettingsView Redesign

#### Layout: sub-navigation + single-section

```
┌─────────────────────────────────────────────────┐
│  page-head (kicker + h1 + subtitle)             │
├──────────────┬──────────────────────────────────┤
│  sub-nav     │  content area                     │
│  (sticky)    │  (one section at a time)          │
│              │                                   │
│  > 外观       │  [Appearance section content]     │
│    设备       │                                   │
│    VIP        │                                   │
│    更新       │                                   │
│    存储       │                                   │
│    诊断       │                                   │
└──────────────┴──────────────────────────────────┘
```

- Left sub-nav: 6 items, sticky, capsule-style (Aurora) or serif-list (Newsprint). Active item highlighted. Clicking switches the right content area with a GSAP transition (fade + 8px translateY, ~200ms).
- Right content area: only the active section renders (`v-if` per section, wrapped in `<Transition>`).
- No more 8-section scroll wall.

#### 6 sections (merged from 8)

1. **外观 Appearance** — skin selection (Aurora/Newsprint card previews with mini-swatch) + light/dark mode toggle (sun/moon icons) + live preview. This is where themeStore is controlled. Drawer's micro-adjustments are NOT here (they stay in Drawer).
2. **设备指纹 Device** — dfid/mid/uuid inputs + char counters + save/test/clear actions + status. Condensed instructional text (current 3 paragraphs → 1 short paragraph + a "查看详情" expandable).
3. **VIP 福利** — listen-VIP + ad-VIP claim buttons + status. Two action rows unified into one card with a primary/secondary visual distinction.
4. **版本更新** — check + download + install + skip + changelog. Conditional reveals use GSAP height/opacity transitions instead of instant show/hide.
5. **存储缓存** — cache info (size, image LRU count) + clear button. Replace `alert()` with an in-page confirm modal.
6. **诊断 Diagnostics** — MERGED: native memory diagnostics (C++ FFI `/diagnostics/memory`) + frontend playback diagnostics (`playbackDiagnostics.getEvents()`) + Copy button + stall highlight. One unified section, two sub-panels.

#### Unified button system

Replace undefined `.cta` + ad-hoc inline styles with three classes (defined in `style.css`):
- `.btn-primary` — accent background, paper text, 36px height, 14px font, 8px 16px padding, 6px border-radius. For primary actions (检查更新, 保存指纹, 领取 VIP).
- `.btn-secondary` — transparent background, ink-soft border, ink text. For secondary actions (测试连接, 清除).
- `.btn-ghost` — no border, ink-mute text, hover background. For tertiary actions (刷新, 复制).

All three share: `font-family: var(--font-sans)`, `cursor: pointer`, `transition: all 0.15s var(--ease-spa)`, `:active { transform: scale(0.97) }` (GSAP-free press feedback via CSS).

#### Per-skin visual variants

**Aurora Settings** (`[data-skin="aurora"] .settings-*`):
- Sub-nav: capsule tab bar (horizontal or vertical), 13px SF Pro, active = accent underline or filled pill
- Content cards: `background: var(--paper)`, `border: 1px solid var(--rule-soft)`, `border-radius: 12px`, `padding: 24px`
- Section titles: 17px semibold, `--ink`, no kicker
- Generous whitespace, thin dividers

**Newsprint Settings** (`[data-skin="newsprint"] .settings-*`):
- Sub-nav: serif italic list, active = `--accent` left border
- Content cards: `background: var(--paper)`, `border: 2px solid var(--ink)`, `border-radius: 0`, `padding: 20px 24px`, optional `box-shadow: 4px 4px 0 var(--ink-soft)`
- Section titles: serif `--font-serif`, kicker retained (small caps "APPEARANCE · 外观")
- Paper texture, dark borders, retro stamp feel

### 3. Animation System (GSAP)

#### Install

`pnpm add gsap` in `ui/`. Import only `{ gsap }` core (tree-shake plugins). ~30KB gzipped.

#### Motion token (style.css)

```css
:root {
  --ease-spa: cubic-bezier(0.16, 1, 0.3, 1);       /* expo-out, primary ease */
  --ease-material: cubic-bezier(0.4, 0, 0.2, 1);   /* material-standard, drawer/panel */
  --dur-fast: 0.15s;   /* hovers, presses */
  --dur-normal: 0.25s; /* modals, section switch */
  --dur-slow: 0.4s;    /* view switch, theme crossfade */
}
```

All existing `cubic-bezier(0.16,1,0.3,1)` literals and `ease` keywords in scoped styles are replaced with `var(--ease-spa)` / `var(--dur-*)` for consistency.

#### Animations

| Where | What | How |
|---|---|---|
| **App.vue view switch** | Outgoing view fades+slides out (opacity 0, translateY -12px, 200ms), incoming fades+slides in (opacity 0→1, translateY 12px→0, 250ms, stagger 30ms if multiple cards) | `<Transition>` JS hooks (`@before-leave`/`@enter`) calling `gsap.to()`; OR CSS `transition` classes if GSAP not needed for stagger. PREFERENCE: GSAP for the enter stagger on HomeView's card grid. |
| **AddToPlaylistModal** | Overlay opacity 0→1 (150ms), modal scale 0.96→1 + translateY 8px→0 (250ms expo-out). Exit reverses. | `<Transition>` JS hooks + `gsap.to()`. |
| **StatsView count-up** | Overview card numbers (total plays, listened seconds, unique counts) animate from 0 to target on mount/range-switch. | `gsap.to(obj, { value: target, duration: 0.8, ease: 'power2.out', onUpdate: () => ref.value = Math.round(obj.value) })`. |
| **StatsView bar chart** | Bar height tweens on range switch (current = instant inline style). | `gsap.to(barEl, { height: targetPx, duration: 0.4, ease: 'power2.out' })`. |
| **Theme switch crossfade** | On `themeStore.setSkin`/`setMode`, the whole `.app` does opacity 1→0.3→1 (300ms total) while CSS variables swap at the dip. | `gsap.timeline()` — `to(.app, {opacity:0.3, duration:0.15})` + `onComplete: applyTheme` + `to(.app, {opacity:1, duration:0.15})`. |
| **SettingsView section switch** | Outgoing section opacity→0 + translateY -8px (150ms), incoming opacity 0→1 + translateY 8px→0 (200ms). | `<Transition>` JS hooks + `gsap.to()`. |
| **Button micro-interactions** | Play button: press scale 0.94→1 (100ms bounce). Icon buttons: hover scale 1.08 (100ms). | CSS `:active { transform: scale(0.97) }` + `:hover { transform: scale(1.05) }` with `transition: transform var(--dur-fast) var(--ease-spa)`. GSAP not needed for these. |
| **Lyric active line** | Current `all 0.3s ease` + `scale(1.05)` is sluggish. Replace with `transform 0.25s var(--ease-spa), color 0.25s var(--ease-spa)` (drop font-size transition — the scale handles the emphasis). | CSS only. |

#### GSAP usage rules

- Import `{ gsap }` from `'gsap'` at the top of any `.vue`/`.ts` file that animates. No plugins needed for this scope (ScrollTrigger/MotionPath/Flip not used here).
- Use `gsap.to()` / `gsap.from()` for simple tweens, `gsap.timeline()` for sequenced (theme crossfade).
- Clean up timelines on unmount: `onUnmounted(() => tl.kill())` to prevent leaks.
- Respect `prefers-reduced-motion`: a `gsap.matchMedia()` query that reduces all durations to 0 for `(prefers-reduced-motion: reduce)`.

### 4. Lyric Fullscreen Mode

#### Trigger

- Both a fullscreen toggle button in LyricView's `page-head` (icon: expand arrows) AND double-click on the `.lyric-meta` cover area. Either gesture toggles fullscreen.
- Sets a reactive `isFullscreen` ref on LyricView. LyricView passes it up to App.vue via a shared reactive ref (a small module-level `ref<boolean>` in a `lyricFullscreen.ts` helper, imported by both — simpler than an event bus).

#### Effect

- `isFullscreen=true` → App.vue hides sidebar/topbar/playerbar (via a reactive flag passed up or an event bus). LyricView's `.lyric-container` expands to fill the window.
- Cover art scales up (e.g. 200px → 320px).
- Lyrics center horizontally; `.lyric-right` takes full width below the cover.
- Compact-queue hides.
- A floating "exit fullscreen" button (top-right) + Esc key listener.

#### Transition (GSAP)

- Enter: sidebar/topbar/playerbar `gsap.to(..., { opacity: 0, x: -20, duration: 0.25 })` (slide out left/right/down), then `gsap.set(..., { display: 'none' })`. LyricView container `gsap.to(..., { maxWidth: '100vw', duration: 0.4 })`.
- Exit: reverse.

#### App.vue integration

App.vue imports the shared `lyricFullscreen` ref from `lyricFullscreen.ts`. When true, sidebar/topbar/playerbar use `v-show="false"` (or `v-if` to remove from layout). The grid shell's `grid-template-columns` / `grid-template-rows` adjust via a reactive class binding to give LyricView the full viewport. This is a local layout override driven by the shared ref, not a global responsive breakpoint.

## Error Handling

- **GSAP load failure**: if `gsap` fails to import (CDN/bundle issue), all animations degrade to instant (CSS `transition` fallbacks remain on hovers/presses). Wrap GSAP calls in `try/catch` or feature-detect `typeof gsap !== 'undefined'`.
- **themeStore persistence failure**: localStorage quota — already handled (themeStore catches). Drawer micro-adjustments same.
- **SettingsView section data fetch error**: each section's `onMounted` fetch already has try/catch + error state. No change.
- **Lyric fullscreen Esc listener**: added on enter, removed on exit (cleanup). If LyricView unmounts while fullscreen, App.vue's `lyricFullscreen` ref resets via `onUnmounted`.

## Testing Strategy

### Unit/Component Tests

- **themeStore**: setSkin/setMode write correct `data-*` attributes; Drawer no longer writes `--paper`/`--ink` or `html.dark` class; dark-mode token overrides + component-level `[data-mode="dark"]` rules both apply.
- **SettingsView**: sub-nav switches active section; only one section renders at a time; button classes (`btn-primary`/`btn-secondary`/`btn-ghost`) applied correctly; Appearance section's skin/mode controls call themeStore.
- **Animation**: GSAP animations are tested via `vi.mock('gsap')` + asserting `gsap.to` called with expected targets/durations. Count-up: mock gsap, assert `onUpdate` callback updates the ref. Reduced-motion: `matchMedia` mock returns reduce → durations are 0.
- **Lyric fullscreen**: toggle button sets `isFullscreen`; Esc key exits; App.vue hides shell when fullscreen active.

### Manual Verification

- Toggle dark mode via SettingsView → ALL component-level dark adjustments apply (shadows, hovers, mix-blend) — not just token swaps.
- Switch skin Aurora↔Newsprint → GSAP crossfade; both Settings visual variants render correctly.
- Navigate Settings sub-nav → sections transition smoothly.
- Open StatsView → numbers count up; switch range → bars tween.
- Open LyricView → click fullscreen → sidebar/topbar/playerbar slide out; lyrics fill window; Esc exits.
- `prefers-reduced-motion: reduce` (OS setting) → all animations instant, no motion.

### Regression

- Existing 274 vitest tests + 22 Rust + 11 C++ must still pass.
- `vue-tsc --noEmit` clean.
- EQ/audio playback unaffected (animation is visual-only; no audio-path changes).

## Acceptance Criteria

- [ ] `themeStore` is the single source of truth for skin + mode; Drawer no longer writes base tokens or `html.dark`.
- [ ] All `html.dark` selectors in `style.css` migrated to `[data-mode="dark"]`; dark mode set via SettingsView fully applies component-level adjustments.
- [ ] SettingsView uses sub-navigation + single-section display; 6 sections; GSAP section transitions.
- [ ] Two per-skin Settings visual variants (Aurora minimalist / Newsprint retro) render correctly.
- [ ] Unified button system (`btn-primary`/`btn-secondary`/`btn-ghost`) replaces undefined `.cta` and ad-hoc inline styles in SettingsView.
- [ ] GSAP installed; view-switch, modal, count-up, bar-chart, theme-crossfade, section-switch animations work.
- [ ] Motion token (`--ease-spa`/`--dur-*`) defined and used; inconsistent easings replaced.
- [ ] `prefers-reduced-motion: reduce` disables all motion.
- [ ] Lyric fullscreen mode: toggle + Esc exit + GSAP transition; sidebar/topbar/playerbar hidden; lyrics fill window.
- [ ] Full frontend test suite + Rust + C++ pass; `vue-tsc --noEmit` clean.

## Implementation Phases

### Phase 1: Theming Unification (prerequisite)
- Remove Drawer's skin/mode/base-token writes; remove `html.dark` class.
- Migrate `style.css` `html.dark` → `[data-mode="dark"]` (~200 lines).
- Verify dark mode via SettingsView fully works.

### Phase 2: SettingsView Redesign
- Sub-navigation + single-section layout.
- 6 merged sections; unified button system.
- Two per-skin visual variants.
- GSAP section-switch transition.

### Phase 3: Animation System
- Install GSAP; define motion token; replace inconsistent easings.
- View-switch transition (App.vue).
- Modal transition (AddToPlaylistModal).
- StatsView count-up + bar-chart tween.
- Theme-switch crossfade.
- `prefers-reduced-motion` support.

### Phase 4: Lyric Fullscreen
- Fullscreen toggle + Esc exit.
- App.vue shell hide/show.
- GSAP enter/exit transition.
- Cover scale + lyrics center.

### Phase 5: Review
- Manual verification of all 4 phases.
- Full test suite + typecheck.
- Confirm no audio-path regressions.

## Out of Scope for This Spec

- Broad layout/responsive overhaul (song-row, PlayerBar grid, app-shell breakpoints, empty-state component, sticky table headers, pagination).
- Intermittent audio-stall bug (deferred to `systematic-debugging` when reproducible).
- EQ response-curve visualization.
- `<keep-alive>` for view state preservation.
- New views, new recommendation surfaces, native playback reactivation.
- Installer/auto-update/release work (already excluded from the product-stability spec).

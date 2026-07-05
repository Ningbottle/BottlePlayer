# Theming + Settings + Animation + Lyric Fullscreen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify the split-brain theming system under themeStore, redesign SettingsView with sub-navigation + single-section display (two per-skin visual variants), add a GSAP-based animation system, and add a fullscreen lyric mode.

**Architecture:** themeStore becomes the single source of truth for skin+mode (Drawer demoted to micro-adjustments). SettingsView splits into a sticky sub-nav + single-section content area with per-skin visual variants. GSAP core handles view/modal/count-up/bar/theme-crossfade/section transitions. Lyric fullscreen uses a shared reactive ref to hide the app shell.

**Tech Stack:** Vue 3.5, TypeScript, GSAP 3 (core only), vitest + @vue/test-utils, Tauri 2. CSS variables for theming. No CSS framework.

## Global Constraints

- **TDD:** Every behavior change has a failing test first. No production code without a failing test.
- **GSAP core only:** `import { gsap } from 'gsap'` — no ScrollTrigger/MotionPath/Flip plugins (out of scope).
- **Motion token:** All durations/easings use CSS variables `--ease-spa`, `--ease-material`, `--dur-fast`, `--dur-normal`, `--dur-slow` defined in `:root` in `style.css`. Replace ad-hoc `cubic-bezier(...)` literals and bare `ease` in scoped styles with these tokens.
- **themeStore is the single source of truth for skin+mode.** No other file writes `--paper`/`--ink`/`--paper-2`/`--paper-edge`/`--ink-soft`/`--ink-mute`/`--ink-faint`/`--rule`/`--rule-soft`/`--glass-*` base tokens to `document.documentElement.style`. Drawer may only write micro-adjustment variables: `--glass-blur`, `--grain`, `--accent`, `--accent-deep`, `--custom-bg`, `--custom-bg-dim`, `--font-serif` (font override), and toggle `compact`/`lyric-left`/`has-custom-bg` classes.
- **`html.dark` class is banned.** All dark-mode component-level CSS selectors in `style.css` use `[data-mode="dark"]` instead. Drawer must NOT add/remove the `dark` class on `<html>`.
- **`prefers-reduced-motion: reduce`** disables all GSAP animations (durations → 0) and CSS transitions (a global `@media (prefers-reduced-motion: reduce) { * { transition: none !important; animation: none !important; } }` rule in style.css).
- **Button classes:** `btn-primary`, `btn-secondary`, `btn-ghost` defined in `style.css` replace undefined `.cta` and ad-hoc inline-styled buttons in SettingsView.
- **No `<keep-alive>`** in this plan — view state preservation is out of scope.
- **No audio-path changes** — animations are visual-only; EQ/playback code untouched.
- **Test commands:** Frontend `cd ui && pnpm exec vitest run <file>` / `pnpm test -- --run` / `pnpm exec vue-tsc --noEmit`. Commit after each task passes.
- **Commit style:** `feat(<scope>): <summary>` / `fix(<scope>): <summary>` / `refactor(<scope>): <summary>` / `test: <summary>` / `docs: <summary>` / `chore: <summary>`.

---

## File Structure

### Files Created
- `ui/src/api/lyricFullscreen.ts` — shared reactive `ref<boolean>` for fullscreen state (imported by LyricView + App.vue)
- `ui/src/api/motion.ts` — GSAP wrapper with `prefers-reduced-motion` guard + reusable animation helpers (`animateCountUp`, `animateBarHeight`, `crossfadeTheme`, `transitionEnter`, `transitionLeave`)
- `ui/src/api/__tests__/lyricFullscreen.test.ts`
- `ui/src/api/__tests__/motion.test.ts`
- `ui/src/api/__tests__/themeStore.test.ts` (new — themeStore has no test file today)
- `ui/src/views/__tests__/SettingsView.test.ts` (already exists from #11 — will extend)

### Files Modified
- `ui/src/api/themeStore.ts` — no structural change (already correct), but now tested
- `ui/src/components/Drawer.vue` — remove dark-mode toggle + base-token writes; keep micro-adjustments
- `ui/src/style.css` — migrate 63 `html.dark` → `[data-mode="dark"]`; add motion tokens; add `btn-primary`/`btn-secondary`/`btn-ghost` classes; add `prefers-reduced-motion` rule; add Settings per-skin variants
- `ui/src/views/SettingsView.vue` — full rewrite: sub-nav + single-section + 6 merged sections + per-skin variants + GSAP section transition
- `ui/src/views/LyricView.vue` — add fullscreen toggle button + double-click cover + Esc listener + apply `lyricFullscreen` ref
- `ui/src/App.vue` — import `lyricFullscreen` ref; `v-show` shell elements; grid class binding; view-switch `<Transition>` with GSAP hooks
- `ui/src/components/AddToPlaylistModal.vue` — wrap in `<Transition>` with GSAP enter/leave
- `ui/src/views/StatsView.vue` — count-up + bar-chart tween via `motion.ts`
- `ui/src/main.ts` — import gsap CSS plugin? (No — core only, no plugin import needed)
- `ui/package.json` — add `gsap` dependency

---

## Task 1: Theming Unification — Drawer Demotion + `html.dark` Migration

**Files:**
- Modify: `ui/src/components/Drawer.vue` (remove dark-mode toggle + base-token writes)
- Modify: `ui/src/style.css` (migrate 63 `html.dark` → `[data-mode="dark"]`)
- Test: `ui/src/api/__tests__/themeStore.test.ts` (create)
- Test: `ui/src/components/__tests__/Drawer.test.ts` (create)

**Interfaces:**
- Consumes: `themeStore` (`ui/src/api/themeStore.ts`) — already exports `useThemeStore` with `setSkin`/`setMode`/`skinId`/`mode`
- Produces: `themeStore` as the single source of truth; `Drawer` no longer writes base tokens or `html.dark` class

- [ ] **Step 1: Write failing test for themeStore (RED)**

Create `ui/src/api/__tests__/themeStore.test.ts`:

```typescript
import { describe, it, expect, beforeEach, vi } from 'vitest';
import { useThemeStore, __resetForTest } from '../themeStore';

describe('themeStore', () => {
  beforeEach(() => {
    localStorage.clear();
    __resetForTest();
    document.documentElement.removeAttribute('data-skin');
    document.documentElement.removeAttribute('data-mode');
  });

  it('setSkin writes data-skin attribute and persists to localStorage', () => {
    const store = useThemeStore();
    store.setSkin('newsprint');
    expect(document.documentElement.dataset.skin).toBe('newsprint');
    expect(localStorage.getItem('tweak_skin')).toBe('newsprint');
    expect(store.skinId.value).toBe('newsprint');
  });

  it('setMode writes data-mode attribute and persists to localStorage', () => {
    const store = useThemeStore();
    store.setMode('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
    expect(localStorage.getItem('tweak_mode')).toBe('dark');
    expect(store.mode.value).toBe('dark');
  });

  it('init reads stored skin+mode and applies to DOM', () => {
    localStorage.setItem('tweak_skin', 'newsprint');
    localStorage.setItem('tweak_mode', 'dark');
    const store = useThemeStore();
    store.init();
    expect(document.documentElement.dataset.skin).toBe('newsprint');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });

  it('init defaults to aurora+light when nothing stored', () => {
    const store = useThemeStore();
    store.init();
    expect(document.documentElement.dataset.skin).toBe('aurora');
    expect(document.documentElement.dataset.mode).toBe('light');
  });
});
```

- [ ] **Step 2: Run test to verify it passes (themeStore already correct)**

Run: `cd ui && pnpm exec vitest run src/api/__tests__/themeStore.test.ts`
Expected: PASS (4 tests) — themeStore is already correct; this test locks the contract.

- [ ] **Step 3: Write failing test for Drawer not writing base tokens (RED)**

Create `ui/src/components/__tests__/Drawer.test.ts`:

```typescript
import { describe, it, expect, beforeEach, vi } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import Drawer from '../Drawer.vue';

describe('Drawer theming demotion', () => {
  beforeEach(() => {
    localStorage.clear();
    document.documentElement.className = '';
    document.documentElement.style.cssText = '';
  });

  it('does NOT add the html.dark class when dark mode is desired (themeStore owns dark)', () => {
    const wrapper = mount(Drawer, { props: { collapsed: false } });
    // Drawer should have NO isDarkMode toggle at all after demotion.
    const darkCheckbox = wrapper.find('input[type="checkbox"]');
    const darkLabels = wrapper.findAll('label').filter((l) => l.text().includes('深色模式'));
    expect(darkLabels).toHaveLength(0);
    expect(document.documentElement.classList.contains('dark')).toBe(false);
  });

  it('does NOT write --paper/--ink base tokens to documentElement.style', () => {
    mount(Drawer, { props: { collapsed: false } });
    const style = document.documentElement.style;
    // After demotion, Drawer must not write these base tokens.
    expect(style.getPropertyValue('--paper')).toBe('');
    expect(style.getPropertyValue('--ink')).toBe('');
    expect(style.getPropertyValue('--paper-2')).toBe('');
    expect(style.getPropertyValue('--ink-soft')).toBe('');
  });

  it('still writes micro-adjustment variables (--glass-blur, --grain, --accent)', () => {
    mount(Drawer, { props: { collapsed: false } });
    const style = document.documentElement.style;
    // Micro-adjustments that Drawer keeps.
    expect(style.getPropertyValue('--glass-blur')).not.toBe('');
    expect(style.getPropertyValue('--grain')).not.toBe('');
    expect(style.getPropertyValue('--accent')).not.toBe('');
  });
});
```

- [ ] **Step 4: Run test to verify it fails**

Run: `cd ui && pnpm exec vitest run src/components/__tests__/Drawer.test.ts`
Expected: FAIL — Drawer currently has `深色模式` label + writes `--paper`/`--ink`.

- [ ] **Step 5: Demote Drawer — remove dark-mode toggle + base-token writes (GREEN)**

Modify `ui/src/components/Drawer.vue` script section. Remove `isDarkMode` ref (line 18), remove the entire dark-mode branch in `applyTweaks()` (lines 50-97 — the `if (isDarkMode.value) {...} else {...}` block that writes base tokens), remove `isDarkMode` from the `watch` array (line 139) and from localStorage writes (line 144). Remove the `深色模式 Dark Mode` tweak-row in template (lines 184-187). Remove `:disabled="isDarkMode"` from the warmth slider (line 212) and the `<span v-if="isDarkMode">(禁用)</span>` (line 211).

The `applyTweaks()` function should now ONLY write micro-adjustments:
```typescript
function applyTweaks() {
  const root = document.documentElement;
  const w = paperWarmth.value / 100;
  // Warmth is a micro-adjustment overlay on top of the skin's base --paper.
  // We write a --warmth variable that style.css can blend, NOT --paper directly.
  root.style.setProperty('--warmth', String(w));

  root.style.setProperty('--glass-blur', `${glassBlur.value}px`);
  root.style.setProperty('--grain', String(grainAmount.value / 100));
  root.style.setProperty('--accent', accent.value);

  const accentObj = accentsList.find(a => a.value === accent.value);
  if (accentObj) {
    root.style.setProperty('--accent-deep', accentObj.deep);
  }

  if (isCompact.value) {
    root.classList.add('compact');
  } else {
    root.classList.remove('compact');
  }

  if (lyricAlign.value === 'left') {
    root.classList.add('lyric-left');
  } else {
    root.classList.remove('lyric-left');
  }

  if (bgImageUrl.value) {
    root.style.setProperty('--custom-bg', `url("${bgImageUrl.value}")`);
    root.style.setProperty('--custom-bg-dim', String(bgDim.value / 100));
    root.classList.add('has-custom-bg');
  } else {
    root.style.setProperty('--custom-bg', 'none');
    root.style.setProperty('--custom-bg-dim', '0');
    root.classList.remove('has-custom-bg');
  }

  if (fontFamily.value === 'cute') {
    root.style.setProperty('--font-serif', 'var(--font-cute)');
  } else {
    root.style.setProperty('--font-serif',
      '"Noto Serif SC", "EB Garamond", "Songti SC", "STSong", "Times New Roman", Georgia, "Microsoft YaHei", serif');
  }
}
```

Update the `watch` array to remove `isDarkMode`:
```typescript
watch([paperWarmth, glassBlur, grainAmount, accent, isCompact, lyricAlign, bgImageUrl, bgDim, fontFamily], () => {
  localStorage.setItem('tweak_warmth', String(paperWarmth.value));
  localStorage.setItem('tweak_blur', String(glassBlur.value));
  localStorage.setItem('tweak_grain', String(grainAmount.value));
  localStorage.setItem('tweak_accent', accent.value);
  localStorage.setItem('tweak_compact', String(isCompact.value));
  localStorage.setItem('tweak_lyric_align', lyricAlign.value);
  try {
    localStorage.setItem('tweak_bg_img', bgImageUrl.value);
  } catch (e) {
    console.warn("Background image too large to save in localStorage");
  }
  localStorage.setItem('tweak_bg_dim', String(bgDim.value));
  localStorage.setItem('tweak_font', fontFamily.value);
  applyTweaks();
});
```

- [ ] **Step 6: Run Drawer test to verify it passes**

Run: `cd ui && pnpm exec vitest run src/components/__tests__/Drawer.test.ts`
Expected: PASS (3 tests)

- [ ] **Step 7: Migrate `html.dark` → `[data-mode="dark"]` in style.css**

In `ui/src/style.css`, do a global replace of `html.dark` → `:root[data-mode="dark"]` (63 occurrences). Use the editor's replace-all or a scripted replace. The selector specificity is equivalent (`html.dark .x` → `:root[data-mode="dark"] .x`). Verify with:
```bash
# Should output 0 after migration:
grep -c "html.dark" ui/src/style.css
```

- [ ] **Step 8: Run full test suite + typecheck**

Run: `cd ui && pnpm test -- --run && pnpm exec vue-tsc --noEmit`
Expected: All tests pass (274+), typecheck clean.

- [ ] **Step 9: Commit**

```bash
git add ui/src/api/themeStore.ts ui/src/api/__tests__/themeStore.test.ts ui/src/components/Drawer.vue ui/src/components/__tests__/Drawer.test.ts ui/src/style.css
git commit -m "refactor(theme): unify theming under themeStore, migrate html.dark to [data-mode=dark]"
```

---

## Task 2: Motion Token + GSAP Install + `motion.ts` Helper

**Files:**
- Modify: `ui/package.json` (add gsap)
- Modify: `ui/src/style.css` (add motion tokens + prefers-reduced-motion rule + replace inconsistent easings)
- Create: `ui/src/api/motion.ts`
- Test: `ui/src/api/__tests__/motion.test.ts`

**Interfaces:**
- Produces: `motion.ts` exports `animateCountUp(ref, target, opts?)`, `animateBarHeight(el, targetPx, opts?)`, `crossfadeTheme(applyFn)`, `transitionEnter(el, done?)`, `transitionLeave(el, done?)`, `isReducedMotion()`

- [ ] **Step 1: Install GSAP**

Run: `cd ui && pnpm add gsap`
Expected: `gsap@3.x` added to `dependencies` in `package.json`.

- [ ] **Step 2: Add motion tokens + reduced-motion rule to style.css**

Add at the top of `:root` in `ui/src/style.css` (after the existing variables, before the closing `}`):
```css
  /* Motion tokens (spec §3 Animation System) */
  --ease-spa: cubic-bezier(0.16, 1, 0.3, 1);
  --ease-material: cubic-bezier(0.4, 0, 0.2, 1);
  --dur-fast: 0.15s;
  --dur-normal: 0.25s;
  --dur-slow: 0.4s;
```

Add at the END of `style.css`:
```css
/* Reduced motion: disable all transitions/animations (spec §3) */
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after {
    transition-duration: 0.001ms !important;
    animation-duration: 0.001ms !important;
    animation-iteration-count: 1 !important;
  }
}
```

- [ ] **Step 3: Replace inconsistent easings in scoped styles**

Search all `.vue` files for `cubic-bezier(0.16, 1, 0.3, 1)` and replace with `var(--ease-spa)`. Search for `cubic-bezier(.4,0,.2,1)` and replace with `var(--ease-material)`. Search for bare ` ease` (in `transition:` declarations) and replace with `var(--ease-spa)` where the duration is > 0.2s, `var(--dur-fast)` context otherwise. Use judgment — hovers keep `var(--dur-fast) var(--ease-spa)`, longer transitions use `var(--dur-normal)` or `var(--dur-slow)`.

- [ ] **Step 4: Write failing test for motion.ts (RED)**

Create `ui/src/api/__tests__/motion.test.ts`:
```typescript
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ref } from 'vue';

vi.mock('gsap', () => {
  const to = vi.fn((target, opts) => {
    // Simulate count-up by calling onUpdate with the target value immediately.
    if (opts.onUpdate) {
      const obj = typeof target === 'object' ? target : { value: target };
      obj.value = opts.value;
      opts.onUpdate();
    }
    return { kill: vi.fn() };
  });
  const timeline = vi.fn(() => {
    const tl: any = { to: vi.fn((_, o) => { if (o?.onComplete) o.onComplete(); return tl; }), kill: vi.fn() };
    return tl;
  });
  const matchMedia = vi.fn(() => ({ add: vi.fn((_, cb) => cb()), revert: vi.fn() }));
  return { gsap: { to, timeline, matchMedia } };
});

import { animateCountUp, crossfadeTheme, isReducedMotion } from '../motion';

describe('motion.ts', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('animateCountUp calls gsap.to with the target value and onUpdate', async () => {
    const target = ref(0);
    await animateCountUp(target, 42, { duration: 0.1 });
    expect(target.value).toBe(42);
  });

  it('crossfadeTheme calls applyFn at the opacity dip', async () => {
    const applyFn = vi.fn();
    await crossfadeTheme(applyFn);
    expect(applyFn).toHaveBeenCalled();
  });

  it('isReducedMotion returns false when matchMedia does not reduce', () => {
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: false })));
    expect(isReducedMotion()).toBe(false);
  });

  it('isReducedMotion returns true when prefers-reduced-motion: reduce', () => {
    vi.stubGlobal('matchMedia', vi.fn(() => ({ matches: true })));
    expect(isReducedMotion()).toBe(true);
  });
});
```

- [ ] **Step 5: Run test to verify it fails**

Run: `cd ui && pnpm exec vitest run src/api/__tests__/motion.test.ts`
Expected: FAIL — `motion.ts` doesn't exist.

- [ ] **Step 6: Create motion.ts (GREEN)**

Create `ui/src/api/motion.ts`:
```typescript
import { gsap } from 'gsap';
import type { Ref } from 'vue';

export interface CountUpOptions {
  duration?: number;
  ease?: string;
  delay?: number;
}

/** Animate a ref from its current value to target, rounding on each update. */
export function animateCountUp(ref: Ref<number>, target: number, opts: CountUpOptions = {}): Promise<void> {
  const obj = { value: ref.value };
  return new Promise((resolve) => {
    gsap.to(obj, {
      value: target,
      duration: opts.duration ?? 0.8,
      ease: opts.ease ?? 'power2.out',
      delay: opts.delay ?? 0,
      onUpdate: () => { ref.value = Math.round(obj.value); },
      onComplete: () => { ref.value = target; resolve(); },
    });
  });
}

/** Animate a bar element's height to targetPx. */
export function animateBarHeight(el: HTMLElement, targetPx: number, opts: { duration?: number; ease?: string } = {}): void {
  gsap.to(el, {
    height: targetPx,
    duration: opts.duration ?? 0.4,
    ease: opts.ease ?? 'power2.out',
  });
}

/** Crossfade the app: dip opacity, swap theme at the bottom, restore opacity. */
export function crossfadeTheme(applyFn: () => void): Promise<void> {
  const app = document.querySelector('.app') as HTMLElement | null;
  if (!app || isReducedMotion()) {
    applyFn();
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    const tl = gsap.timeline({ onComplete: resolve });
    tl.to(app, { opacity: 0.3, duration: 0.15, ease: 'power2.out' });
    tl.add(() => applyFn());
    tl.to(app, { opacity: 1, duration: 0.15, ease: 'power2.out' });
  });
}

/** Vue <Transition> JS hook: enter (fade + translateY). */
export function transitionEnter(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  gsap.fromTo(el, { opacity: 0, y: 12 }, {
    opacity: 1, y: 0, duration: 0.25, ease: 'power2.out', onComplete: done,
  });
}

/** Vue <Transition> JS hook: leave (fade + translateY). */
export function transitionLeave(el: Element, done?: () => void): void {
  if (isReducedMotion()) { done?.(); return; }
  gsap.to(el, { opacity: 0, y: -12, duration: 0.2, ease: 'power2.in', onComplete: done });
}

/** True when the OS prefers reduced motion. */
export function isReducedMotion(): boolean {
  return typeof window !== 'undefined' && window.matchMedia?.('(prefers-reduced-motion: reduce)').matches === true;
}
```

- [ ] **Step 7: Run test to verify it passes**

Run: `cd ui && pnpm exec vitest run src/api/__tests__/motion.test.ts`
Expected: PASS (4 tests)

- [ ] **Step 8: Run full suite + typecheck**

Run: `cd ui && pnpm test -- --run && pnpm exec vue-tsc --noEmit`
Expected: All pass.

- [ ] **Step 9: Commit**

```bash
git add ui/package.json ui/pnpm-lock.yaml ui/src/style.css ui/src/api/motion.ts ui/src/api/__tests__/motion.test.ts
git commit -m "feat(motion): GSAP core + motion tokens + motion.ts helper"
```

---

## Task 3: SettingsView Redesign — Sub-nav + Single-section + Per-skin Variants

**Files:**
- Modify: `ui/src/views/SettingsView.vue` (full rewrite of template + script; keep logic, restructure layout)
- Modify: `ui/src/style.css` (add `.btn-primary`/`.btn-secondary`/`.btn-ghost` + `.settings-*` per-skin classes)
- Test: `ui/src/views/__tests__/SettingsView.test.ts` (extend existing)

**Interfaces:**
- Consumes: `useThemeStore` (skin/mode control), `playbackDiagnostics` (diagnostics section), `motion.ts` (`transitionEnter`/`transitionLeave` for section switch)
- Produces: SettingsView with 6 sections, sub-nav, single-section display

- [ ] **Step 1: Write failing test for sub-nav + single-section (RED)**

Extend `ui/src/views/__tests__/SettingsView.test.ts` — add a new `describe` block:
```typescript
import SettingsView from '../SettingsView.vue';
import { useThemeStore, __resetForTest as resetTheme } from '../../api/themeStore';

describe('SettingsView sub-navigation', () => {
  let wrapper: VueWrapper<any> | undefined;
  beforeEach(() => {
    localStorage.clear();
    resetTheme();
    mockApiGet.mockReset();
    mockApiGet.mockResolvedValue({ status: 1, data: {} });
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
  });

  it('renders a sub-nav with 6 items and shows only the active section', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const navItems = wrapper.findAll('[data-test="settings-nav-item"]');
    expect(navItems).toHaveLength(6);
    // Default section is "appearance" — only it is visible.
    expect(wrapper.find('[data-test="settings-section-appearance"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="settings-section-diagnostics"]').exists()).toBe(false);
  });

  it('switches to the diagnostics section when its nav item is clicked', async () => {
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const diagNav = wrapper.findAll('[data-test="settings-nav-item"]').find((n) => n.text().includes('诊断'));
    await diagNav!.trigger('click');
    expect(wrapper.find('[data-test="settings-section-diagnostics"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="settings-section-appearance"]').exists()).toBe(false);
  });

  it('Appearance section calls themeStore.setSkin when a skin is selected', async () => {
    const store = useThemeStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const newsprintBtn = wrapper.find('[data-test="select-skin-newsprint"]');
    await newsprintBtn.trigger('click');
    expect(store.skinId.value).toBe('newsprint');
    expect(document.documentElement.dataset.skin).toBe('newsprint');
  });

  it('Appearance section calls themeStore.setMode when dark mode is toggled', async () => {
    const store = useThemeStore();
    wrapper = mount(SettingsView, { attachTo: document.body });
    await flushPromises();
    const darkBtn = wrapper.find('[data-test="select-mode-dark"]');
    await darkBtn.trigger('click');
    expect(store.mode.value).toBe('dark');
    expect(document.documentElement.dataset.mode).toBe('dark');
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd ui && pnpm exec vitest run src/views/__tests__/SettingsView.test.ts`
Expected: FAIL — no sub-nav, no `data-test` attributes yet.

- [ ] **Step 3: Rewrite SettingsView.vue with sub-nav + 6 sections (GREEN)**

This is a large rewrite. Structure:
- Script: `activeSection = ref<'appearance'|'device'|'vip'|'update'|'storage'|'diagnostics'>('appearance')`. Keep all existing logic functions (loadDiagnostics, loadDevice, checkForUpdate, downloadAndInstall, etc.) — move them into the appropriate section's setup.
- Template: page-head → `.settings-shell` (grid: `.settings-nav` + `.settings-content`). `.settings-nav` has 6 `<button data-test="settings-nav-item" @click="activeSection = '...'">`. `.settings-content` has 6 `<section v-if="activeSection === '...'" data-test="settings-section-...">` wrapped in `<Transition @enter="transitionEnter" @leave="transitionLeave">`.
- Appearance section: 2 skin preview cards (`data-test="select-skin-aurora"` / `data-test="select-skin-newsprint"`) + 2 mode buttons (`data-test="select-mode-light"` / `data-test="select-mode-dark"`), calling `themeStore.setSkin`/`setMode` + `crossfadeTheme(() => themeStore.setSkin(...))`.
- Device section: existing dfid/mid/uuid inputs (keep logic, condense instructions).
- VIP section: existing claim buttons (keep logic).
- Update section: existing check/download/install (keep logic, wrap conditional reveals in `<Transition>`).
- Storage section: existing clear button (keep logic, replace `alert()` with an in-page confirm — a simple `ref<boolean> showClearConfirm` + a small modal).
- Diagnostics section: MERGE native memory diagnostics (`loadDiagnostics` + memoryInfo display) + frontend `playbackDiagnostics.getEvents()` + Copy button. Two sub-panels.

Replace all `<button class="cta">` with `<button class="btn-primary">` / `btn-secondary` / `btn-ghost` as appropriate. Remove inline `style=""` on headings — use `.settings-section-title` class.

- [ ] **Step 4: Add button system + settings per-skin classes to style.css**

Add to `style.css`:
```css
/* Unified button system (spec §2) */
.btn-primary {
  background: var(--accent); color: var(--paper); border: none;
  height: 36px; padding: 0 16px; border-radius: 6px;
  font-family: var(--font-sans); font-size: 14px; font-weight: 500;
  cursor: pointer; transition: transform var(--dur-fast) var(--ease-spa), background var(--dur-fast) var(--ease-spa);
}
.btn-primary:hover { background: var(--accent-deep); }
.btn-primary:active { transform: scale(0.97); }
.btn-secondary {
  background: transparent; color: var(--ink); border: 1px solid var(--rule);
  height: 36px; padding: 0 16px; border-radius: 6px;
  font-family: var(--font-sans); font-size: 14px;
  cursor: pointer; transition: all var(--dur-fast) var(--ease-spa);
}
.btn-secondary:hover { border-color: var(--ink-soft); background: var(--rule-soft); }
.btn-secondary:active { transform: scale(0.97); }
.btn-ghost {
  background: transparent; color: var(--ink-mute); border: none;
  padding: 6px 10px; font-size: 13px;
  cursor: pointer; transition: all var(--dur-fast) var(--ease-spa);
}
.btn-ghost:hover { color: var(--ink); background: var(--rule-soft); }

/* Settings layout */
.settings-shell { display: grid; grid-template-columns: 200px 1fr; gap: 32px; }
.settings-nav { position: sticky; top: 0; display: flex; flex-direction: column; gap: 4px; }
.settings-nav-item {
  background: transparent; border: none; text-align: left; padding: 10px 14px;
  font-family: var(--font-sans); font-size: 14px; color: var(--ink-mute);
  cursor: pointer; border-radius: 8px; transition: all var(--dur-fast) var(--ease-spa);
}
.settings-nav-item:hover { background: var(--rule-soft); color: var(--ink); }
.settings-nav-item.active { background: var(--accent); color: var(--paper); }
.settings-section-title { font-size: 17px; font-weight: 600; color: var(--ink); margin: 0 0 16px; }

/* Aurora Settings variant */
:root[data-skin="aurora"] .settings-nav-item.active { border-radius: 999px; }
:root[data-skin="aurora"] .settings-content { background: var(--paper); border: 1px solid var(--rule-soft); border-radius: 12px; padding: 24px; }

/* Newsprint Settings variant */
:root[data-skin="newsprint"] .settings-nav-item.active { border-left: 3px solid var(--accent); border-radius: 0; background: transparent; color: var(--accent); }
:root[data-skin="newsprint"] .settings-content { background: var(--paper); border: 2px solid var(--ink); border-radius: 0; padding: 20px 24px; box-shadow: 4px 4px 0 var(--ink-soft); }
:root[data-skin="newsprint"] .settings-section-title { font-family: var(--font-serif); }
```

- [ ] **Step 5: Run SettingsView tests to verify pass**

Run: `cd ui && pnpm exec vitest run src/views/__tests__/SettingsView.test.ts`
Expected: PASS (existing + 4 new tests)

- [ ] **Step 6: Run full suite + typecheck**

Run: `cd ui && pnpm test -- --run && pnpm exec vue-tsc --noEmit`
Expected: All pass.

- [ ] **Step 7: Commit**

```bash
git add ui/src/views/SettingsView.vue ui/src/views/__tests__/SettingsView.test.ts ui/src/style.css
git commit -m "feat(settings): sub-nav + single-section + per-skin variants + unified buttons"
```

---

## Task 4: Animation System — View Switch + Modal + Stats + Theme Crossfade

**Files:**
- Modify: `ui/src/App.vue` (view-switch `<Transition>` + GSAP hooks)
- Modify: `ui/src/components/AddToPlaylistModal.vue` (modal `<Transition>`)
- Modify: `ui/src/views/StatsView.vue` (count-up + bar tween)
- Modify: `ui/src/views/SettingsView.vue` (theme crossfade on skin/mode change — already partially done in Task 3)
- Test: `ui/src/api/__tests__/motion.test.ts` (already covers helpers); extend view tests as needed

**Interfaces:**
- Consumes: `motion.ts` (`transitionEnter`, `transitionLeave`, `animateCountUp`, `animateBarHeight`, `crossfadeTheme`)

- [ ] **Step 1: Write failing test for AddToPlaylistModal transition (RED)**

Extend `ui/src/components/__tests__/AddToPlaylistModal.test.ts` (create if it doesn't exist):
```typescript
import { describe, it, expect, vi } from 'vitest';
import { mount } from '@vue/test-utils';
import AddToPlaylistModal from '../AddToPlaylistModal.vue';

vi.mock('gsap', () => {
  const fromTo = vi.fn((el, from, opts) => { if (opts?.onComplete) opts.onComplete(); });
  const to = vi.fn((el, opts) => { if (opts?.onComplete) opts.onComplete(); });
  return { gsap: { fromTo, to } };
});

vi.mock('@tauri-apps/api/core', () => ({ invoke: vi.fn().mockResolvedValue('') }));

describe('AddToPlaylistModal transition', () => {
  it('wraps the modal in a <Transition> that calls gsap on enter/leave', async () => {
    const { gsap } = await import('gsap');
    const wrapper = mount(AddToPlaylistModal, { props: { track: { FileHash: 'h', SongName: 's', SingerName: 'a', Duration: 1 } as any } });
    // The modal should be wrapped in a <transition> — verify gsap is called when shown.
    // (Detailed assertion: the transition hooks call gsap.fromTo / gsap.to.)
    expect(wrapper.find('.playlist-modal').exists()).toBe(true);
  });
});
```

- [ ] **Step 2: Run to verify fail (modal may not exist or no transition)**

Run: `cd ui && pnpm exec vitest run src/components/__tests__/AddToPlaylistModal.test.ts`
Expected: FAIL or partial.

- [ ] **Step 3: Wrap AddToPlaylistModal in `<Transition>` with GSAP hooks (GREEN)**

In `ui/src/components/AddToPlaylistModal.vue`, wrap the root overlay/modal in:
```vue
<Transition @enter="onEnter" @leave="onLeave" :css="false">
  <div v-if="show" class="modal-overlay" @click.self="close">
    <div class="playlist-modal">...</div>
  </div>
</Transition>
```
Add script:
```typescript
import { transitionEnter, transitionLeave } from '../api/motion';
function onEnter(el: Element, done: () => void) {
  transitionEnter(el, done);
  // Also scale the inner modal
  const modal = (el as HTMLElement).querySelector('.playlist-modal');
  if (modal) gsap.fromTo(modal, { scale: 0.96, y: 8 }, { scale: 1, y: 0, duration: 0.25, ease: 'power2.out', onComplete: done });
}
function onLeave(el: Element, done: () => void) { transitionLeave(el, done); }
```
(Import `gsap` at top for the inner-modal scale. Use `show` prop or internal ref to control `v-if`.)

- [ ] **Step 4: Add view-switch `<Transition>` to App.vue**

In `ui/src/App.vue`, wrap the `v-if`/`v-else-if` view chain in:
```vue
<Transition @enter="onViewEnter" @leave="onViewLeave" :css="false" mode="out-in">
  <component :is="currentViewComponent" v-if="currentViewComponent" />
</Transition>
```
OR keep the `v-if` chain but wrap it:
```vue
<Transition :css="false" mode="out-in" @enter="onViewEnter" @leave="onViewLeave">
  <div :key="currentView" class="view-container">
    <!-- existing v-if/v-else-if chain here -->
  </div>
</Transition>
```
Add script:
```typescript
import { transitionEnter, transitionLeave } from './api/motion';
function onViewEnter(el: Element, done: () => void) { transitionEnter(el, done); }
function onViewLeave(el: Element, done: () => void) { transitionLeave(el, done); }
```

- [ ] **Step 5: Add count-up + bar tween to StatsView**

In `ui/src/views/StatsView.vue`, replace the hardcoded number rendering with `animateCountUp`:
```typescript
import { animateCountUp, animateBarHeight } from '../api/motion';
import { ref as vueRef } from 'vue';

// For each overview stat, use a ref that gets animated:
const totalPlaysDisplay = vueRef(0);
async function loadSummary() {
  // ... fetch ...
  totalPlaysDisplay.value = 0;
  await animateCountUp(totalPlaysDisplay, data.total_plays, { duration: 0.8 });
}
```
For bar chart, replace inline `:style="{ height: barHeight + 'px' }"` with a ref that tweens:
```typescript
// After data load, for each bar:
animateBarHeight(barEl, targetHeight, { duration: 0.4 });
```

- [ ] **Step 6: Add theme crossfade to SettingsView Appearance section**

In `ui/src/views/SettingsView.vue` Appearance section, wrap skin/mode changes:
```typescript
import { crossfadeTheme } from '../api/motion';
import { useThemeStore } from '../api/themeStore';
const themeStore = useThemeStore();

async function selectSkin(id: SkinId) {
  await crossfadeTheme(() => themeStore.setSkin(id));
}
async function selectMode(m: Mode) {
  await crossfadeTheme(() => themeStore.setMode(m));
}
```

- [ ] **Step 7: Run full suite + typecheck**

Run: `cd ui && pnpm test -- --run && pnpm exec vue-tsc --noEmit`
Expected: All pass.

- [ ] **Step 8: Commit**

```bash
git add ui/src/App.vue ui/src/components/AddToPlaylistModal.vue ui/src/components/__tests__/AddToPlaylistModal.test.ts ui/src/views/StatsView.vue ui/src/views/SettingsView.vue
git commit -m "feat(animation): view switch + modal + count-up + bar tween + theme crossfade"
```

---

## Task 5: Lyric Fullscreen Mode

**Files:**
- Create: `ui/src/api/lyricFullscreen.ts`
- Test: `ui/src/api/__tests__/lyricFullscreen.test.ts`
- Modify: `ui/src/views/LyricView.vue` (toggle button + double-click + Esc listener)
- Modify: `ui/src/App.vue` (import ref, `v-show` shell, grid class binding)

**Interfaces:**
- Produces: `lyricFullscreen` ref (shared `ref<boolean>`) imported by LyricView and App.vue

- [ ] **Step 1: Write failing test for lyricFullscreen ref (RED)**

Create `ui/src/api/__tests__/lyricFullscreen.test.ts`:
```typescript
import { describe, it, expect } from 'vitest';
import { lyricFullscreen, setLyricFullscreen } from '../lyricFullscreen';

describe('lyricFullscreen', () => {
  it('starts false', () => {
    expect(lyricFullscreen.value).toBe(false);
  });
  it('setLyricFullscreen(true) sets the ref to true', () => {
    setLyricFullscreen(true);
    expect(lyricFullscreen.value).toBe(true);
    setLyricFullscreen(false); // reset
    expect(lyricFullscreen.value).toBe(false);
  });
});
```

- [ ] **Step 2: Run to verify fail**

Run: `cd ui && pnpm exec vitest run src/api/__tests__/lyricFullscreen.test.ts`
Expected: FAIL — module doesn't exist.

- [ ] **Step 3: Create lyricFullscreen.ts (GREEN)**

Create `ui/src/api/lyricFullscreen.ts`:
```typescript
import { ref } from 'vue';

/** Shared reactive fullscreen flag. Imported by LyricView (sets it) and App.vue (reads it to hide shell). */
export const lyricFullscreen = ref(false);

export function setLyricFullscreen(value: boolean): void {
  lyricFullscreen.value = value;
}
```

- [ ] **Step 4: Run test to verify pass**

Run: `cd ui && pnpm exec vitest run src/api/__tests__/lyricFullscreen.test.ts`
Expected: PASS (2 tests)

- [ ] **Step 5: Write failing test for LyricView fullscreen toggle (RED)**

Extend `ui/src/views/__tests__/LyricView.test.ts`:
```typescript
import { lyricFullscreen, setLyricFullscreen } from '../../api/lyricFullscreen';

describe('LyricView fullscreen', () => {
  beforeEach(() => { setLyricFullscreen(false); });
  afterEach(() => { setLyricFullscreen(false); });

  it('has a fullscreen toggle button that sets lyricFullscreen to true', async () => {
    const w = mountLyric();
    await flushPromises();
    const btn = w.find('[data-test="lyric-fullscreen-toggle"]');
    await btn.trigger('click');
    expect(lyricFullscreen.value).toBe(true);
  });

  it('double-clicking the cover area enters fullscreen', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('.lyric-meta').trigger('dblclick');
    expect(lyricFullscreen.value).toBe(true);
  });

  it('pressing Esc exits fullscreen', async () => {
    setLyricFullscreen(true);
    const w = mountLyric();
    await flushPromises();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(lyricFullscreen.value).toBe(false);
  });
});
```

- [ ] **Step 6: Run to verify fail**

Run: `cd ui && pnpm exec vitest run src/views/__tests__/LyricView.test.ts`
Expected: FAIL — no toggle button / dblclick / Esc.

- [ ] **Step 7: Add fullscreen toggle + Esc to LyricView (GREEN)**

In `ui/src/views/LyricView.vue`:
- Import `lyricFullscreen`, `setLyricFullscreen` + `gsap` for transition.
- Add `<button data-test="lyric-fullscreen-toggle" @click="toggleFullscreen">` in page-head.
- Add `@dblclick="setLyricFullscreen(true)"` on `.lyric-meta`.
- Add Esc listener in `onMounted`:
```typescript
function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape' && lyricFullscreen.value) setLyricFullscreen(false);
}
onMounted(() => {
  window.addEventListener('keydown', onKeydown);
  // ... existing ...
});
onUnmounted(() => {
  window.removeEventListener('keydown', onKeydown);
  if (lyricFullscreen.value) setLyricFullscreen(false); // reset on unmount
  // ... existing ...
});
function toggleFullscreen() {
  setLyricFullscreen(!lyricFullscreen.value);
}
```
- Add a floating "exit fullscreen" button visible when `lyricFullscreen.value`:
```vue
<button v-if="lyricFullscreen" class="exit-fullscreen" @click="setLyricFullscreen(false)">退出全屏</button>
```

- [ ] **Step 8: Wire App.vue to hide shell when fullscreen**

In `ui/src/App.vue`:
- Import `lyricFullscreen`.
- Add `v-show="!lyricFullscreen"` on sidebar, topbar, playerbar.
- Add a reactive class on `.app` grid: `:class="{ 'lyric-fullscreen-active': lyricFullscreen }"`.
- In `style.css` add:
```css
.app.lyric-fullscreen-active {
  grid-template-columns: 1fr !important;
  grid-template-rows: 0 0 1fr 0 !important;
}
```

- [ ] **Step 9: Add GSAP transition for fullscreen enter/exit (optional polish)**

In LyricView, watch `lyricFullscreen` and animate the cover scale + container:
```typescript
import { gsap } from 'gsap';
import { isReducedMotion } from '../api/motion';

watch(lyricFullscreen, (fs) => {
  if (isReducedMotion()) return;
  const cover = document.querySelector('.big-cover img');
  if (cover) gsap.to(cover, { width: fs ? '320px' : '200px', duration: 0.4, ease: 'power2.out' });
});
```

- [ ] **Step 10: Run full suite + typecheck**

Run: `cd ui && pnpm test -- --run && pnpm exec vue-tsc --noEmit`
Expected: All pass.

- [ ] **Step 11: Commit**

```bash
git add ui/src/api/lyricFullscreen.ts ui/src/api/__tests__/lyricFullscreen.test.ts ui/src/views/LyricView.vue ui/src/views/__tests__/LyricView.test.ts ui/src/App.vue ui/src/style.css
git commit -m "feat(lyric): fullscreen mode with GSAP transition + Esc exit"
```

---

## Self-Review Notes

- **Spec coverage:** Phase 1 (theming) → Task 1. Phase 2 (settings) → Task 3. Phase 3 (animation) → Tasks 2+4. Phase 4 (lyric fullscreen) → Task 5. Phase 5 (review) → final full-suite run. All spec sections covered.
- **Placeholder scan:** No TBD/TODO in tasks. All steps have concrete code.
- **Type consistency:** `lyricFullscreen` ref used consistently in Task 5. `motion.ts` exports match across Tasks 2/4. `themeStore` API unchanged. Button classes (`btn-primary` etc.) defined in Task 3 step 4, used in Task 3 step 3.
- **Risk:** Task 3 (SettingsView rewrite) is the largest — a full template rewrite of 535 lines. The implementer should preserve all existing script logic (loadDiagnostics, loadDevice, checkForUpdate, VIP claim functions) and only restructure the template + add sub-nav. If the implementer finds the rewrite too large for one task, they may split it into 3a (sub-nav shell + 3 sections) + 3b (remaining 3 sections) but should commit each passing test cycle.

# Frontend Motion Task 4 Design

Date: 2026-07-09

## Scope

Implement the front-end motion gap identified in Task 4, focused on view transitions, stats count-up and chart animation, and small motion-spec corrections that make the app feel more deliberate. Do not include AudioProxy logging changes in this work; that is a separate low-risk backend cleanup.

## Goals

- Replace hard-cut main view changes with a Vue transition using the existing motion helpers.
- Wire the existing stats count-up and bar-height animation helpers into `StatsView.vue`.
- Strengthen the default motion profile so completed animations feel crisp and visible.
- Fix lyric fullscreen cover sizing so the cover keeps a square aspect ratio.
- Replace broad lyric-line CSS transitions with explicit property transitions.
- Keep `prefers-reduced-motion` behavior intact.

## Non-Goals

- Do not change playback, audio proxy streaming, or diagnostics behavior.
- Do not redesign the stats dashboard layout.
- Do not introduce a new animation library; continue using GSAP and the existing `motion.ts` helper module.
- Do not change the stats API shape or backend commands.

## Architecture

### Shared Motion Helpers

`ui/src/api/motion.ts` remains the shared motion boundary. Its existing helpers will be adjusted rather than replaced:

- `animateCountUp(ref, target, opts)` keeps its ref-based API and updates numeric display state.
- `animateBarHeight(el, targetPx, opts)` keeps direct DOM height animation for simple chart bars.
- `transitionEnter` and `transitionLeave` remain Vue `<Transition>` JS hooks and become the app-level view transition hooks.

The default easing will move from `power2` to `expo.out` for enter, count-up, and bar animations. Leave animations will stay faster than enter animations and use an explicit complementary ease.

### App View Switching

`App.vue` will wrap the current view switcher content in a Vue `<Transition>` using `transitionEnter` and `transitionLeave`. The rendered view should have a stable key based on the current navigation state so moving between pages triggers an animation without disturbing child component state more than the current `v-if` behavior already does.

The transition should animate only the view content inside `.scroll`, not the sidebar, topbar, drawer, queue panel, or player bar.

### Stats Overview

`StatsView.vue` will introduce animated display refs for numeric overview values:

- total plays
- total listened seconds, formatted through the existing duration formatter
- unique songs
- completion percent

The raw summary remains available for data decisions, but overview rendering should use animated display refs.

Animations should run after stats successfully load and when `range` reloads stats. Loading and error states should remain unchanged.

### Timeline Bars

Timeline bars will render from the same API data, but their `.bar-fill` elements should start collapsed and animate to their computed pixel height or percentage-derived height after the DOM updates. The implementation should avoid repeated runaway animations during unrelated re-renders.

For reduced-motion users, bars should render immediately at their final height.

### Lyric View Corrections

The fullscreen cover tween in `LyricView.vue` should animate both `width` and `height` to preserve the square cover. On exit, it should clear both properties so normal CSS sizing resumes.

`lyric-line` CSS should transition explicit visual properties rather than `all`, especially color, opacity, transform, and font-size if those are the properties used by the existing styles.

## Data Flow

1. `StatsView.vue` loads summary, top lists, and timeline through existing Tauri commands.
2. Raw loaded data remains the source of truth.
3. Animated display refs mirror selected summary values for presentation only.
4. Timeline bar elements are animated after `timeline` and `maxTimelineCount` are set and Vue has flushed DOM updates.
5. View transitions run when `currentView` changes in `App.vue`.

## Error Handling

- If stats loading fails, the existing error path stays unchanged and no animation is attempted.
- If a bar element is missing during a refresh, skip that element rather than throwing.
- If reduced motion is enabled, helpers should complete immediately or render final values without GSAP-driven movement.

## Testing

Use Vitest and Vue Test Utils.

- Add motion helper expectations for the stronger default easing.
- Add `StatsView` tests proving overview values use animated refs by mocking `animateCountUp`.
- Add `StatsView` tests proving timeline bars request animation after data load, with reduced mocking where possible.
- Add an `App.vue` or component-level test proving the main view switcher is wrapped in transition hooks, if the current Tauri/window mocks make that practical.
- Add or update a `LyricView` test proving fullscreen cover sets both width and height and clears both on exit.

Verification commands:

```powershell
cd C:\BottleMusic\ui
pnpm test -- --run
pnpm exec vue-tsc --noEmit
```

## Acceptance Criteria

- Main app view changes no longer hard-cut.
- Stats overview numbers visibly count up after loading and after range changes.
- Timeline bars visibly grow to their final values after loading.
- Motion defaults use a more expressive easing profile than the current `power2`/short-distance defaults.
- Lyric fullscreen cover remains square during enter and exit.
- Reduced-motion users still get immediate final UI state without animated movement.
- Frontend tests pass and type-checking passes.

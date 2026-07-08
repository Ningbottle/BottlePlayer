# SDD Progress Ledger

Base: 7ec81a18
Branch: product-stability

## Tasks

Task 1: complete (commits 7ec81a18..9b095282, review: concerns resolved - html.dark=0 verified)
Task 2: complete (commits 7ec81a18..f7fe05b1, review: concerns resolved - stash dropped)
Wave 1 BASE for Wave 2: 9b095282
Task 3+ (settings + lyric-fullscreen + motion-on-modal): complete (this wave)
  - SettingsView rewritten to sub-nav + single-section (6 sections) + per-skin variants
  - lyricFullscreen shared ref + LyricView toggle/dblclick/Esc + App.vue shell hide
  - AddToPlaylistModal wrapped in <Transition> with GSAP enter/leave (rendered via <Teleport to="body">)
  - Unified button system (.btn-primary/.btn-secondary/.btn-ghost) + settings layout CSS added to style.css
  - 2 previously-failing GSAP modal tests fixed (root cause: @vue/test-utils stubs <Transition> by default)
  - Verify: vue-tsc --noEmit clean; vitest 291/291 pass (35 files)
  - DEFERRED to a follow-up wave (Task 4 Steps 4-5):
      * Step 4 — App.vue view-switch <Transition> with GSAP hooks (not implemented)
      * Step 5 — StatsView count-up + bar-chart GSAP tween via motion.ts (not implemented)
    The spec acceptance criterion "view-switch, modal, count-up, bar-chart, theme-crossfade,
    section-switch animations work" is therefore partially unmet (modal + theme-crossfade +
    section-switch are in; view-switch + count-up + bar-chart are out). Tracked for a follow-up wave.

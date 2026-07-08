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
  - AddToPlaylistModal wrapped in <Transition> with GSAP enter/leave
  - Unified button system (.btn-primary/.btn-secondary/.btn-ghost) + settings layout CSS added to style.css
  - 2 previously-failing GSAP modal tests fixed (root cause: @vue/test-utils stubs <Transition> by default)
  - Verify: vue-tsc --noEmit clean; vitest 291/291 pass (35 files)

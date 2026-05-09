# EchoMusic Native Skills And Frontend Intake

## Installed Skills

Installed from `mattpocock/skills` on 2026-05-02:

- `setup-matt-pocock-skills`
- `grill-me`
- `improve-codebase-architecture`
- `tdd`
- `design-an-interface`
- `to-issues`

Restart Codex to pick up new skills.

Notes:

- `design-an-interface` is currently under `skills/deprecated` in `mattpocock/skills`. Use it as a design-analysis aid, not as the long-term source of truth.
- `setup-matt-pocock-skills` should be run first after restart if the skill asks to initialize repository-specific instructions.
- The current project remains driven by `native/docs/*` and the C++ source; skills are workflow helpers, not a replacement for local project truth.

## How To Provide Frontend Images

Preferred input is screenshots or image references. Use this format:

- Page name: startup, home, search, playlist detail, player bar, lyric, comments, settings, desktop lyric.
- State: normal, loading, empty, error, playing, paused, modal, drawer, hover, selected.
- Keep: note any region that should be preserved closely.
- Avoid: note any region that should not be copied.
- Priority: mark images as `must match`, `directional`, or `reference only`.

Useful image sets:

- One full-window screenshot per core page.
- One screenshot per overlay or drawer.
- One cropped screenshot for dense controls such as playback, tabs, filters, right-click menus, queue drawer, and desktop lyric.
- Optional annotated screenshots with red circles or labels.

## Other Accepted Inputs

- Existing Electron screenshots from `screenshots/`.
- Existing Vue components and routes from `src/renderer`.
- Figma links for precise spacing and typography.
- Text notes for interaction rules.
- Hand sketches for early layout intent.

## First Design Baseline

Until new images are supplied, use existing Electron screenshots as the baseline:

- `screenshots/home.png`: main layout, sidebar, recommendation density.
- `screenshots/search.png`, `screenshots/search_hot.png`, `screenshots/search_suggest.png`: search flow.
- `screenshots/playlist.png`: playlist detail and song list density.
- `screenshots/lyric.png`: lyric page.
- `screenshots/song_comment.png`, `screenshots/album_comment.png`, `screenshots/floor_comment.png`: comments.
- `screenshots/settings.png`: settings layout.
- `screenshots/desktop_lyric.png`: desktop lyric.


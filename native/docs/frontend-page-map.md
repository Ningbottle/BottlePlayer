# EchoMusic Native Frontend Page Map

## Current Native Frontend State

`EchoWin32` is only a Direct2D/DirectWrite shell. It creates a Win32 window and draws placeholder text. It does not yet contain product UI, navigation, page state, image loading, hit testing, virtual lists, or playback controls.

The first native frontend milestone should be a usable integration shell, not a complete visual clone.

## Application Shell

Core regions:

- Title area: window drag region, app identity, navigation controls, search entry, window buttons.
- Sidebar: primary navigation, user entry, playlist shortcuts.
- Content viewport: route-specific content, scroll state, loading and empty states.
- Player bar: current track, cover, transport controls, progress, volume, queue, lyric toggle.
- Overlay layer: dialogs, drawers, context menus, toast notifications.

Rendering rules:

- Use Direct2D for shapes and image composition.
- Use DirectWrite for all text.
- Use WIC for image decode and resize.
- Keep fixed-size controls stable; hover and selection must not resize rows or buttons.
- Use virtualized drawing for all song, playlist, album, artist, and comment lists.

## First Pages

### Startup

Purpose: initialize backend, ensure device registration, route into the app.

Data:

- `IBackendFacade::EnsureDeviceReady`.
- Compat server `/health` only for debugging mode.

States:

- Starting backend.
- Registering device.
- Ready.
- Failed with retry.

### Main Home

Purpose: first usable music view.

Baseline:

- `screenshots/home.png`
- `screenshots/discover.png`

Content:

- Recommended playlists.
- Top IP or editorial section.
- Recently relevant entry points if available from local state.

### Search

Purpose: first backend-heavy validation page.

Baseline:

- `screenshots/search.png`
- `screenshots/search_hot.png`
- `screenshots/search_suggest.png`

Data:

- `/search/default`
- `/search/hot`
- `/search/suggest`
- `/search`

States:

- Empty search with hot words.
- Suggest panel while typing.
- Results split by song, album, artist, playlist.
- Loading and no results.

### Playlist Detail

Purpose: validate pagination, virtual list, song actions.

Baseline:

- `screenshots/playlist.png`

Data:

- `/playlist/detail`
- `/playlist/track/all`

Controls:

- Play all.
- Add to queue.
- Favorite/unfavorite where authenticated.
- Song row double-click playback.
- Context menu.

### Player Bar

Purpose: keep playback always available.

Data:

- `PlaybackState`.
- Current `Song`.
- Queue snapshot.

Controls:

- Previous, play/pause, next.
- Progress seek.
- Volume.
- Queue drawer.
- Audio quality/effect entry.
- Main lyric and desktop lyric toggles.

### Lyric

Purpose: validate lyric parsing and synchronized drawing.

Baseline:

- `screenshots/lyric.png`

Data:

- `/search/lyric`
- `/lyric`
- playback progress events.

States:

- No lyric.
- Loading.
- Synced lyric.
- Manual scroll with return-to-current control.

### Comments

Purpose: validate nested data and paging.

Baseline:

- `screenshots/song_comment.png`
- `screenshots/album_comment.png`
- `screenshots/floor_comment.png`

Data:

- `/comment/music`
- `/comment/album`
- `/comment/playlist`
- `/comment/floor`

### Settings

Purpose: local state and native controls.

Baseline:

- `screenshots/settings.png`

Groups:

- Playback.
- Audio output.
- Shortcuts.
- Theme.
- Cache and diagnostics.

### Desktop Lyric

Purpose: native always-on-top low-memory lyric window.

Baseline:

- `screenshots/desktop_lyric.png`

Requirements:

- Separate lightweight top-level window.
- Click-through lock mode.
- Drag, resize, font size, color, always-on-top.
- No web runtime.

## Backend To Frontend Data Boundary

Temporary:

- Electron frontend calls `EchoCompatServer` through HTTP.
- Win32 debug shell may also call compat routes during early bring-up.

Final:

- Win32 UI calls `IBackendFacade` directly.
- UI receives events through `IBackendEventSink` and a UI-thread posted message.
- UI stores only page ViewModels and visible data windows.


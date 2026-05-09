# EchoMusic Native Frontend Image Analysis

## Source Images

Received on 2026-05-02:

- Home surface: Melody-style music dashboard with sidebar, search, recommendation banner, playlist cards, recent playback, artist recommendations, and bottom player bar.
- Now-playing surface: album art, lyric column, playback queue, and bottom player bar.

## Visual Thesis

Warm translucent music workspace: soft white surface, restrained blue accent, image-led cards, dense but breathable lists, and persistent playback.

## Shared Layout

- Window: 1600 x 1060 reference scale.
- Sidebar: 178 px, left fixed, pale warm background, thin right divider.
- Header: 86 px top working area with back/forward, search, profile, and window chrome.
- Bottom player: 96 px persistent playback bar with cover, title, transport, progress, volume, queue, and lyric entry.
- Main content: scrollable area between header and player.
- Radius: 8 px for cards and buttons unless the shape is circular.
- Accent: calm blue for active nav, progress, playback, and synced lyric.

## Home Surface

Regions:

- Sidebar navigation and playlists.
- Greeting block.
- Hero recommendation banner.
- Playlist card rows.
- Recent playback list.
- Artist recommendation strip.
- Bottom player.

Controls:

- Search input.
- Play buttons on banner and cards.
- View all links.
- More menus in recent playback rows.
- Follow buttons in artist section.

## Now Playing Surface

Regions:

- Sidebar navigation.
- Tabs: now playing and lyric.
- Album artwork with vinyl edge.
- Song metadata and progress.
- Center lyric column.
- Queue panel.
- Bottom player.

Controls:

- Favorite.
- More menu.
- Lyric settings.
- Translate.
- Report lyric.
- Queue clear.
- Queue more menu.
- Save playlist.

## First Native Implementation

Implemented as a static Direct2D prototype in `EchoWin32`:

- Press `1` to show the home surface.
- Press `2` to show the now-playing surface.
- Click the active nav row area for home or the lyric button area to switch surfaces later when hit testing is expanded.
- On startup, `EchoWin32` now creates an `IBackendFacade`, calls `EnsureDeviceReady()` asynchronously, and shows the device readiness state in the header.

Planned next:

- Replace placeholder covers with WIC-loaded artwork.
- Add hit testing and hover states.
- Bind sections to `IBackendFacade` and playback events.
- Virtualize song lists before wiring large data.

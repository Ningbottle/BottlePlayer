# BottleMusic

> Unofficial KuGou Concept Edition client for Windows

English | [中文](./README.md)

<!-- logo -->

![CI](https://img.shields.io/github/actions/workflow/status/Ningbottle/BottlePlayer/ci.yml?label=CI)
![Version](https://img.shields.io/github/v/release/Ningbottle/BottlePlayer)
![License](https://img.shields.io/github/license/Ningbottle/BottlePlayer)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-blue)

KuGou Concept Edition has no official website or PC client. BottleMusic brings the Concept Edition experience to PC with dual-skin visuals (Aurora immersive + Newsprint editorial) and full playback, equalizer, and statistics features.

## Screenshots

> Screenshots coming soon

<!-- screenshot: Aurora home -->
<!-- screenshot: Aurora fullscreen lyrics -->
<!-- screenshot: Newsprint home -->
<!-- screenshot: Statistics dashboard -->
<!-- screenshot: Equalizer panel -->

## Features

### Playback
- HTML5 Audio engine with play queue, single-loop / list-loop / shuffle
- Drag-to-seek, quality switching, instant stop on track change

### Equalizer
- 10-band Web Audio API equalizer (31Hz / 62Hz / 125Hz / 250Hz / 500Hz / 1kHz / 2kHz / 4kHz / 8kHz / 16kHz)
- 6 built-in presets, local audio proxy handles cross-origin CDN media automatically
- Degradation banner shown when proxy is unavailable

### Dual Skins
- **Aurora**: Immersive particle effects, gradient wash, fullscreen lyric immersion mode
- **Newsprint**: Newspaper-style typography, minimalist editorial design, dark mode support

### Lyrics
- Auto-follow playback progress (auto-resumes after 3s idle)
- Fullscreen immersion mode, click lyric line to seek

### Statistics
- Play history dashboard: total plays, actual listening time, completion rate, unique songs/artists
- Top lists: most-played songs / artists / albums (grouped by album_id)
- Timeline chart: daily play counts
- DeepSeek AI analysis: personalized listening report based on local play data

### Search
- Song / artist / album search with direct playback or add-to-queue

### Playlists
- Load user playlists (favorites / custom), click to play entire list as queue

### Login
- QR code login, user info / VIP status display
- Daily free VIP claim (listen / ad)

### Auto-Update
- Built-in Tauri updater, checks GitHub Releases for new versions on launch

## Download & Install

Download the latest release from the [Releases page](https://github.com/Ningbottle/BottlePlayer/releases).

**Requirements**: Windows 10/11 x64

Installation: NSIS installer (supports per-user and per-machine install). Launch after installation and future updates will be detected and installed automatically.

## Skins

### Aurora
Immersive design with particle effects and gradient wash. Fullscreen lyric mode features cover display, progress bar, and 3D queue shelf.

<!-- screenshot: Aurora skin showcase -->

### Newsprint
Newspaper-style typography with minimalist editorial design. Supports dark mode.

<!-- screenshot: Newsprint skin showcase -->

## Architecture

```
Vue 3 Frontend (ui/src/)
    |  Tauri IPC
Rust FFI Layer (ui/src-tauri/src/)
    |  extern "C" FFI
C++ Core (native/) -> EchoCAPI.dll
```

BottleMusic uses a three-layer architecture: Vue 3 frontend handles UI and playback control, Rust FFI layer bridges Tauri commands, C++ core handles KuGou API request scheduling, SQLite statistics storage, and Media Foundation interface. Playback uses HTML5 Audio + Web Audio API equalizer.

For full architecture documentation, see [CONTEXT.md](./CONTEXT.md).

## Development

| Tool | Version |
|---|---|
| Node.js | >= 22 |
| pnpm | 11 |
| Rust | stable |
| CMake + MSVC | C++20 |

```powershell
git clone --recurse-submodules https://github.com/Ningbottle/BottlePlayer.git
cd ui
pnpm install
pnpm tauri dev
```

For full development documentation, see [CONTEXT.md](./CONTEXT.md).

## Tech Stack

| Layer | Technologies |
|---|---|
| Frontend | Vue 3, Vite 6, Vanilla CSS, GSAP, Web Audio API |
| Rust FFI | Tauri 2.0, reqwest, tokio |
| C++ Core | MSVC C++20, WinHTTP, Media Foundation, SQLite |
| CI/CD | GitHub Actions, CMake, vcpkg, CTest, Vitest, Cargo |

## Credits

- Backend API implementation references [MakcRe/KuGouMusicApi](https://github.com/MakcRe/KuGouMusicApi)
- Project baseline is KuGou Concept Edition (appid=3116, Lite Salt)

## Disclaimer

This project is for personal learning and technical research only. Music data and copyrights belong to the original platform and copyright holders. Please respect intellectual property and support legitimate music.

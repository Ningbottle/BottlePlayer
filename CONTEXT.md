# CONTEXT.md — BottleMusic v2

> **Read this first** when entering the codebase. It gives a mental model of the project, current status, and where things live.

## Project Overview

BottleMusic is a Tauri 2.0 + Vue 3 + C++ unofficial KuGou Concept Edition music player for Windows. The v2 effort is structured as 5 sub-projects (S1–S5) on a shared FFI boundary.

## Architecture (3 layers)

```
Vue 3 Frontend (ui/src/)
  ├─ playerStore.ts — reactive player state + Web Audio API EQ
  ├─ backend.ts — Tauri invoke wrapper with timeout/retry/circuit-breaker
  ├─ themeStore.ts — skin/mode management (Newsprint + Aurora)
  ├─ playerBackend.ts — PlayerBackend interface
  ├─ html5Backend.ts — HTML5 Audio wrapper (current default)
  ├─ nativeBackend.ts — Native playback via Tauri commands (disabled)
  └─ circuitBreaker.ts — frontend resilience
       │ Tauri IPC
       ▼
Rust FFI (ui/src-tauri/src/)
  ├─ backend_api.rs — CApiHandle (DLL symbol loading), handle_request, event bridge
  ├─ playback.rs — 13 Tauri commands for playback control
  └─ lib.rs — Tauri app setup, invoke_handler registration
       │ extern "C" FFI
       ▼
C++ Core (native/) → EchoCAPI.dll
  ├─ core/C_API.cpp — 20+ C API exports, g_api (shared_ptr), g_playback, g_scheduler
  ├─ core/HttpClient.cpp — WinHTTP with watchdog timeout, retry budget, connection pool
  ├─ core/CompatApi.cpp — KuGou API routes
  ├─ async/RequestScheduler.cpp — 4-worker thread pool, bounded shutdown, per-kind deadlines
  ├─ playback/PlaybackController.cpp — Pimpl wrapper
  ├─ playback/PlaybackControllerMFP.cpp — MFPlay implementation (legacy, works)
  ├─ playback/PlaybackControllerMFS.cpp — IMFMediaSession implementation (broken, disabled)
  ├─ playback/EqualizerMFT.cpp — IMFTransform EQ (code exists, not wired into topology)
  ├─ playback/BiquadFilter.cpp — RBJ biquad math (tested, works)
  └─ storage/Database.cpp — SQLite
```

## Sub-Project Status

| Sub-project | Status | Key deliverable |
|---|---|---|
| **S1 Resilience** | ✅ Complete | 3-layer deadline, CircuitBreaker, bounded Shutdown, HttpClient watchdog |
| **S2 Auto-update/CI** | ✅ Complete | ci.yml, release.yml, sync-version.mjs, skip-version |
| **S3 Skin system** | ✅ Complete | themeStore, Aurora + Newsprint skins, dark mode, FOUC prevention |
| **S4 Playback+EQ** | ⚠️ Partial | HTML5 playback + Web Audio API EQ (working). MFS native path abandoned. |
| **S5 Statistics** | ❌ Not started | Play count, listening history, dashboard |

## S4 Details

- **Default backend**: HTML5 Audio (reliable, cross-platform)
- **EQ implementation**: Web Audio API BiquadFilterNode chain (5 bands: 60/230/910/3600/14000 Hz)
- **EQ UI**: EqualizerPanel.vue in Drawer.vue (right sidebar), uses skin CSS variables
- **MFS (IMFMediaSession)**: Code exists but disabled. Issues: incomplete topology (only source nodes), deadlock (mutex + condition variable), COM lifecycle leaks. Abandoned in favor of Web Audio API.
- **EqualizerMFT (C++ IMFTransform)**: Code exists, unit-testable, but not inserted into MF topology. Kept for reference.
- **C API exports**: 14 EchoPlayback* functions exist and work, but frontend doesn't call them (HTML5 default)
- **Rust FFI**: 13 Tauri commands registered, but not used by frontend (HTML5 default)

## Key Files

| File | Responsibility |
|---|---|
| `ui/src/api/playerStore.ts` | Player state, Web Audio EQ, playTrack/togglePlay/seek |
| `ui/src/api/backend.ts` | Tauri invoke wrapper with S1 resilience |
| `ui/src/api/themeStore.ts` | Skin/mode management |
| `ui/src/components/EqualizerPanel.vue` | EQ UI (5 sliders, 4 presets) |
| `ui/src/components/Drawer.vue` | Right sidebar (EQ, theme, tweaks) |
| `ui/src-tauri/src/backend_api.rs` | CApiHandle, DLL loading, event bridge |
| `ui/src-tauri/src/playback.rs` | 13 Tauri playback commands |
| `native/core/C_API.cpp` | C API exports, g_api, g_playback, g_scheduler |
| `native/core/HttpClient.cpp` | WinHTTP + watchdog + retry budget |
| `native/async/RequestScheduler.cpp` | Thread pool + bounded shutdown |
| `native/playback/PlaybackControllerMFP.cpp` | MFPlay (legacy, works) |
| `native/playback/BiquadFilter.cpp` | RBJ biquad math (tested) |

## Build Commands

```bash
# C++ build (needs vcvars64)
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug'

# Rust build
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml

# Frontend dev server
cd C:\BottleMusic\ui && pnpm tauri dev

# Frontend type-check
cd C:\BottleMusic\ui && pnpm exec vue-tsc --noEmit
```

## Test Commands

```bash
# C++ tests
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && set PATH=C:\BottleMusic\native\vcpkg_installed\x64-windows\bin;%PATH% && ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check --output-on-failure'

# Rust tests
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib

# Frontend tests
cd C:\BottleMusic\ui && pnpm test -- --run
```

## Test Counts (as of 2026-06-24)

- C++ ctest: 10 tests
- Rust cargo test: 6 tests
- Frontend vitest: 62 tests
- **Total: 78 tests**

## Known Issues

1. **MFS native playback broken** — topology resolution fails, deadlock on exit. Disabled, using HTML5 fallback.
2. **CORS risk** — `audio.crossOrigin = 'anonymous'` for Web Audio EQ may break if KuGou CDN doesn't send CORS headers.
3. **Some tests assert old native-first behavior** — need updating to match HTML5 default.
4. **`Music Player.html`** — untracked external file with large diff, not part of v2.

## Environment

- **OS**: Windows 11
- **IDE**: Open Design / opencode
- **Git**: `C:\Users\w1521\.qoderworkcn\bin\git\cmd\git.exe`
- **Rust**: `C:\Users\w1521\.cargo\bin\` (1.96.0)
- **CMake**: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **vcpkg**: `C:\vcpkg\vcpkg.exe` (triplet: x64-windows)
- **Node**: v24.17.0, pnpm v11.8.0
- **docs/ is gitignored** — use `git add -f` to track docs

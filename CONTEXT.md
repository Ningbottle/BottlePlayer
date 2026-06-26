# CONTEXT.md — BottleMusic v2

> **Read this first** when entering the codebase. It gives a mental model of the project, current status, and where things live.

## Project Overview

BottleMusic is a Tauri 2.0 + Vue 3 + C++ unofficial KuGou Concept Edition music player for Windows. The v2 effort is structured as 5 sub-projects (S1–S5) on a shared FFI boundary. All five sub-projects are complete.

## Architecture (3 layers)

```
Vue 3 Frontend (ui/src/)
  ├─ playerStore.ts — reactive player state + coordination (event handler dispatch)
  ├─ playSessionTracker.ts — stats session state machine + seek-immune listened accumulator
  ├─ webAudioEq.ts — Web Audio API EQ graph controller (CORS-gated, safe build order)
  ├─ backend.ts — Tauri invoke wrapper with timeout/retry/circuit-breaker
  ├─ themeStore.ts — skin/mode management (Newsprint + Aurora)
  ├─ playerBackend.ts — PlayerBackend interface
  ├─ html5Backend.ts — HTML5 Audio wrapper (current default, sole event source)
  ├─ nativeBackend.ts — Native playback via Tauri commands (disabled)
  ├─ circuitBreaker.ts — frontend resilience
  └─ views/StatsView.vue — statistics dashboard (overview + top lists + timeline + recent + AI)
       │ Tauri IPC
       ▼
Rust FFI (ui/src-tauri/src/)
  ├─ backend_api.rs — CApiHandle (DLL symbol loading), handle_request, event bridge
  ├─ playback.rs — 13 Tauri commands for playback control (unused — HTML5 default)
  ├─ stats.rs — 6 Tauri commands (stats_record_play, stats_get_summary/top/timeline/recent/recommendations)
  ├─ ai_analysis.rs — DeepSeek v4 flash AI analysis (async, user-provided API key)
  └─ lib.rs — Tauri app setup, invoke_handler registration
       │ extern "C" FFI
       ▼
C++ Core (native/) → EchoCAPI.dll
  ├─ core/C_API.cpp — 30+ C API exports, g_api (shared_ptr), g_playback, g_scheduler, g_stats
  ├─ core/HttpClient.cpp — WinHTTP with watchdog timeout, retry budget, connection pool
  ├─ core/CompatApi.cpp — KuGou API routes
  ├─ async/RequestScheduler.cpp — 4-worker thread pool, bounded shutdown/restart, per-kind deadlines
  ├─ playback/PlaybackController.cpp — Pimpl wrapper
  ├─ playback/PlaybackControllerMFP.cpp — MFPlay implementation (legacy, works, deprecated)
  ├─ playback/PlaybackControllerMFS.cpp — IMFMediaSession implementation (broken, disabled)
  ├─ playback/EqualizerMFT.cpp — IMFTransform EQ (code exists, not wired into topology)
  ├─ playback/BiquadFilter.cpp — RBJ biquad math (tested, works)
  ├─ stats/PlayStatsService.cpp — record + query play history (play_history_v2 table)
  └─ storage/Database.cpp — SQLite (play_history_v2 schema with album_id grouping)
```

## Sub-Project Status

| Sub-project | Status | Key deliverable |
|---|---|---|
| **S1 Resilience** | ✅ Complete | 3-layer deadline, CircuitBreaker, bounded Shutdown/Restart, HttpClient watchdog |
| **S2 Auto-update/CI** | ✅ Complete | ci.yml, release.yml, sync-version.mjs, skip-version, Cargo test gate |
| **S3 Skin system** | ✅ Complete | themeStore, Aurora + Newsprint skins, dark mode, FOUC prevention |
| **S4 Playback+EQ** | ✅ Complete | HTML5 backend + Web Audio API EQ (production), PlaySessionTracker, event ownership |
| **S5 Statistics** | ✅ Complete | PlayStatsService, StatsView, DeepSeek AI analysis, 6 stats Tauri commands |

## S4 Details

- **Default backend**: HTML5 Audio (Html5AudioBackend) — sole source of play/pause/timeupdate/ended/error events
- **Stop cleanup**: `Html5AudioBackend.stop()` unloads the current `src`, so a failed next-track resolve cannot resume stale media.
- **EQ implementation**: Web Audio API BiquadFilterNode chain (5 bands: 60/230/910/3600/14000 Hz), controlled by `webAudioEq.ts`
- **EQ UI**: `EqualizerPanel.vue` in `Drawer.vue` (right sidebar), uses skin CSS variables
- **CORS-gated EQ (#1)**: KuGou CDN sends no CORS headers → EQ graph is skipped for cross-origin non-CORS media, audio plays directly. `eqState.available` exposed to UI; degradation banner shown when EQ is not active.
- **EQ graph build order (#4)**: full filter→gain→destination chain built BEFORE `createMediaElementSource`; throws are safe (element never gets stranded in a disconnected graph)
- **AudioContext lifecycle (#9)**: `webAudioEq.close()` releases context on teardown (HMR-safe)
- **Suspended resume (#10)**: failed `resume()` surfaces via `onSuspendedFail` instead of being swallowed
- **PlaySessionTracker (state machine)**: sessions only open on real `play` event (no ghost sessions on rejected play()); `listened_seconds` is seek-immune (forward deltas 0<Δ<2s count, jumps/backward ignored); `completed` uses accumulator not duration; `setQuality` skip+intend keeps quality accurate
- **Event ownership (#2)**: `Html5AudioBackend.onEvent` is sole event source; `initPlayer` only handles `durationchange`/`loadedmetadata`. Double-`ended` handler that double-fetched `/song/url` is gone.
- **Single-loop replay**: handled in `ended` handler (not `next()`); `intend()` runs before `play()` (Bug A invariant)
- **MFS (IMFMediaSession)**: code exists but disabled. Issues: incomplete topology (only source nodes), deadlock (mutex + condition variable), COM lifecycle leaks. Abandoned in favor of Web Audio API.
- **EqualizerMFT (C++ IMFTransform)**: code exists, unit-testable, but not inserted into MF topology. Kept for reference.
- **C API exports**: 14 `EchoPlayback*` functions exist and work, but frontend doesn't call them (HTML5 default)

## S5 Details

- **Schema**: `play_history_v2` table — song_hash, song_name, singer_name, album_id, album_name, cover_url, duration_seconds, completed, listened_seconds, quality, played_at. Indexed by `played_at` and `song_hash`.
- **Record path**: every play → 1 row. Tracked via `PlaySessionTracker` (skip-immune accumulator) + `stats_record_play` Rust command → `EchoStatsRecordPlay` C API → `PlayStatsService::RecordPlay` (with `SqlEscape` for SQL injection safety)
- **Query endpoints** (6 `EchoStatsGet*` C API → 6 Rust Tauri commands):
  - `stats_get_summary` — total plays, listened seconds, unique songs/artists, completion rate, per range (7d/30d/all)
  - `stats_get_top` — top N by song/artist/**album_id** (albums grouped by `album_id` not `name` to avoid same-name merges)
  - `stats_get_timeline` — daily play counts (`{date: "YYYY-MM-DD", count: N}`)
  - `stats_get_recent` — most recent N plays with full metadata (limit/offset)
  - `stats_get_recommendations` — "for you" based on top artists (local-only, no KuGou API fusion)
- **Thread safety**: `g_stats` guarded by `shared_lock(g_api_rwlock)`; `Database::Execute`/`ExecuteQuery` hold `mutex_`; all 5 query C API functions wrapped in try-catch with safe empty JSON fallback. `EchoShutdown` resets global API/database/stat pointers under the exclusive lifecycle lock.
- **AI analysis**: `ai_analyze` async Tauri command → reqwest → DeepSeek API. User provides API key via localStorage `deepseek_api_key` (password input, never logged). 30s timeout. Chinese prompt, 200-word limit, covers listening habits + music taste + one interesting finding.
- **StatsView.vue**: 4 sections — overview cards (total plays / listened time / completion / unique counts), top lists with album art (song/artist/album), timeline CSS bar chart, recent plays list (with cover + completion badge), AI analysis panel

## Key Files

| File | Responsibility |
|---|---|
| `ui/src/api/playerStore.ts` | Player state coordinator (dispatches to backend, tracker, EQ) |
| `ui/src/api/playSessionTracker.ts` | Stats session state machine + seek-immune accumulator |
| `ui/src/api/webAudioEq.ts` | Web Audio API EQ graph controller (CORS-gated, safe build order) |
| `ui/src/api/backend.ts` | Tauri invoke wrapper with S1 resilience |
| `ui/src/api/themeStore.ts` | Skin/mode management |
| `ui/src/components/EqualizerPanel.vue` | EQ UI (5 sliders, 4 presets, degradation banner) |
| `ui/src/components/Drawer.vue` | Right sidebar (EQ, theme, tweaks) |
| `ui/src/views/StatsView.vue` | Statistics dashboard (overview + top + timeline + recent + AI) |
| `ui/src-tauri/src/backend_api.rs` | CApiHandle, DLL loading, event bridge |
| `ui/src-tauri/src/playback.rs` | 13 Tauri playback commands (unused — HTML5 default) |
| `ui/src-tauri/src/stats.rs` | 6 Tauri stats commands |
| `ui/src-tauri/src/ai_analysis.rs` | DeepSeek AI analysis async command |
| `native/core/C_API.cpp` | C API exports, g_api, g_playback, g_scheduler, g_stats |
| `native/core/HttpClient.cpp` | WinHTTP + watchdog + retry budget |
| `native/async/RequestScheduler.cpp` | Thread pool + bounded shutdown/restart |
| `native/playback/PlaybackControllerMFP.cpp` | MFPlay (legacy, works, deprecated) |
| `native/playback/BiquadFilter.cpp` | RBJ biquad math (tested) |
| `native/stats/PlayStatsService.cpp` | Record + query play history (SQL injection-safe) |
| `native/storage/Database.cpp` | SQLite (play_history_v2 schema) |

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

## Test Counts (as of 2026-06-26)

- C++ ctest: 11 tests
- Rust cargo test: 11 tests
- Frontend vitest: 98 tests
- **Total: 120 tests**

## Known Issues

1. **MFS native playback broken** — topology resolution fails, deadlock on exit. Disabled, using HTML5 fallback. (Will not be fixed — MFS path abandoned in favor of Web Audio API.)
2. **EQ unavailable for KuGou CDN** — cross-origin non-CORS media skips the EQ graph (audio plays directly). UI shows a degradation banner. (Will not be fixed at CORS layer — would require Tauri stream proxy; acceptable UX trade-off.)
3. **`Music Player.html` was rewritten in `0bedf68`** — spec called for a one-line syntax fix at line 673, but the rewrite also normalized formatting and removed dead code. The file is tracked in the repo; not a v2 source file but ships with the app.
4. **Minor findings deferred** (from PR review `0bedf68..ce5233c`):
   - EQ re-init order on repeated `initPlayer` (currently harmless — EQ always disabled via CORS)
   - `onEnded` phase guard (defensive — theoretically impossible to trigger)
   - DeepSeek API URL missing `/v1` prefix (works either way, spec deviation only)

## Language

Domain terms used across issues, refactors, and tests. Use these names; avoid the synonyms listed under _Avoid_.

### Playback

**Backend**:
The playback abstraction behind `PlayerBackend` — e.g. `Html5AudioBackend` (production default) or `NativeBackend` (disabled).
_Avoid_: player, audio engine

**PlaybackOrchestrator** _(planned refactor term — not yet implemented in this tree)_:
The module that will own playback transitions and the ordering between Resolve, PlaySession, and Backend. No `playbackOrchestrator.ts` exists here yet; the implementation lives on the `playback-orchestrator-tdd` branch and this entry becomes live when that work merges.
_Avoid_: playback helper, player coordinator

**Playback transition**:
A change from one playback source or state to another: switching tracks, switching quality, replaying a track, or resuming by reloading a missing source.
_Avoid_: playback action, player operation

**PlaySession**:
One listening session from a real `play` event until `ended` or `stop`, tracked by `PlaySessionTracker`.
_Avoid_: play instance, playback session

**Resolve**:
Turn a song identity into a playable URL via KuGou API routes.
_Avoid_: fetch url, get link

**EQ graph**:
The Web Audio API BiquadFilterNode chain (5 bands), CORS-gated; skipped when CDN media lacks CORS headers.
_Avoid_: equalizer, filter chain

### Statistics

**Listened seconds**:
Actual seconds heard during a PlaySession, accumulated by a seek-immune counter (forward deltas 0<Δ<2s only).
_Avoid_: play duration, actual play time

**Completed**:
Whether a PlaySession counts as finished, based on the listened-seconds accumulator — not raw track duration.
_Avoid_: finished, played through

### Resilience

**Circuit breaker**:
Frontend resilience wrapper in `circuitBreaker.ts` — opens after repeated failures, blocks calls until cooldown.
_Avoid_: fallback, retry handler

**Request**:
A KuGou API call dispatched through the C++ `RequestScheduler` thread pool with per-kind deadlines.
_Avoid_: fetch, HTTP call

## Environment

- **OS**: Windows 11
- **IDE**: Open Design / opencode
- **Git**: `C:\Users\w1521\.qoderworkcn\bin\git\cmd\git.exe`
- **Rust**: `C:\Users\w1521\.cargo\bin\` (1.96.0)
- **CMake**: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **vcpkg**: `C:\vcpkg\vcpkg.exe` (triplet: x64-windows)
- **Node**: v24.17.0, pnpm v11.8.0
- **docs/ is gitignored** — use `git add -f` to track docs

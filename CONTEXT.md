# CONTEXT.md — BottleMusic v2

> **Read this first** when entering the codebase. It gives a mental model of the project, current status, and where things live.

## Project Overview

BottleMusic is a Tauri 2.0 + Vue 3 + C++ unofficial KuGou Concept Edition music player for Windows. The v2 effort is structured as 5 sub-projects (S1–S5) on a shared FFI boundary. All five sub-projects are complete.

## Architecture (3 layers)

```
Vue 3 Frontend (ui/src/)
  ├─ playerStore.ts — reactive player state + coordination (event handler dispatch)
  ├─ playSessionTracker.ts — stats session state machine + seek-immune listened accumulator
  ├─ webAudioEq.ts — Web Audio API AudioWorklet EQ graph controller (proxy-enabled, safe build order)
  ├─ backend.ts — Tauri invoke wrapper (熔断 + 单次超时；重试归 C++ HttpClient)
  ├─ themeStore.ts — skin/mode management (Newsprint + Aurora)
  ├─ playerBackend.ts — PlayerBackend interface (html5 only)
  ├─ html5Backend.ts — HTML5 Audio wrapper (sole production backend + event source)
  ├─ audioProxy.ts — frontend wrapper for audio_proxy_url Tauri command (CORS bypass for CDN media)
  ├─ eqWorkletProcessor.ts — AudioWorklet DSP (RBJ peaking, 10-band)
  ├─ circuitBreaker.ts — frontend resilience
  ├─ recentPlayedStore.ts — local-first recent-played store (Vue reactive + localStorage, FileHash dedupe, mergeRemote)
  ├─ useLyricFollow.ts — lyric auto-follow state machine composable (autoFollowing, 3s idle resume, trackKey reset)
  ├─ playbackDiagnostics.ts — in-memory ring buffer for playback boundary events (track_switch/url_resolve/media_event/proxy_prep/fm_fetch)
  └─ views/StatsView.vue — statistics dashboard (overview + top lists + timeline + recent + AI)
       │ Tauri IPC
       ▼
Rust FFI (ui/src-tauri/src/)
  ├─ backend_api.rs — CApiHandle (DLL symbol loading), handle_request, abandoned-worker-safe shutdown
  ├─ stats.rs — 6 Tauri commands (stats_record_play, stats_get_summary/top/timeline/recent/recommendations)
  ├─ ai_analysis.rs — DeepSeek AI analysis (async, shared reqwest Client, user-provided API key)
  ├─ audio_proxy.rs — local HTTP proxy (loopback, CORS + range/resume + SSRF allowlist, shared Client)
  └─ lib.rs — Tauri app setup, invoke_handler registration
       │ extern "C" FFI
       ▼
C++ Core (native/) → EchoCAPI.dll
  ├─ core/C_API.cpp — request/stats exports, Ctx().api (shared_ptr), Ctx().scheduler, Ctx().stats, Ctx().db (no native playback)
  ├─ core/HttpClient.cpp — WinHTTP: unique GET retry owner, Post no-retry, watchdog, connection pool
  ├─ core/CompatApi.cpp — KuGou API routes (sole request dispatch)
  ├─ async/RequestScheduler.cpp — 4-worker thread pool, bounded shutdown/restart, per-kind deadlines
  ├─ stats/PlayStatsService.cpp — record + query play history (play_history_v2 table)
  └─ storage/Database.cpp — SQLite WAL + busy_timeout (play_history_v2 schema)
```

## Sub-Project Status

| Sub-project | Status | Key deliverable |
|---|---|---|
| **S1 Resilience** | ✅ Complete | 3-layer deadline, CircuitBreaker, bounded Shutdown/Restart, HttpClient watchdog |
| **S2 Auto-update/CI** | ✅ Complete | ci.yml, release.yml, sync-version.mjs, skip-version, Cargo test gate |
| **S3 Skin system** | ✅ Complete | themeStore, Aurora + Newsprint skins, dark mode, FOUC prevention |
| **S4 Playback+EQ** | ✅ Complete | HTML5 backend + Web Audio API EQ (production), PlaySessionTracker, event ownership |
| **S5 Statistics** | ✅ Complete | PlayStatsService, StatsView, DeepSeek AI analysis, 6 stats Tauri commands |

## Dual-interface redesign (2026-07)

| Item | Status |
|---|---|
| Aurora / Newsprint independent shells (Home, PlayerBar, LyricStage) | ✅ Closeout complete |
| Home keep-alive + `homeFeedStore` | ✅ |
| Skin-differentiated Sidebar / Topbar chrome | ✅ |
| Enriched Aurora empty queue rail | ✅ |
| Search / Playlist `SkinPageHeader` | ✅ |
| Verification report | `docs/superpowers/reports/2026-07-12-dual-interface-closeout-verification.md` |

**Dev:** after merge to `main`, run from repo root `ui/` (`pnpm tauri dev` or `pnpm dev`). Historical worktree: `.worktrees/dual-interface-player-redesign` (optional to remove after merge).

**Closeout plan/spec:** `docs/superpowers/specs/2026-07-12-dual-interface-closeout-design.md`, `docs/superpowers/plans/2026-07-12-dual-interface-closeout.md`.

## S4 Details

- **Default backend**: HTML5 Audio (Html5AudioBackend) — sole source of play/pause/timeupdate/ended/error events
- **Stop cleanup**: `Html5AudioBackend.stop()` unloads the current `src`, so a failed next-track resolve cannot resume stale media.
- **EQ implementation**: Web Audio API AudioWorklet graph (10 bands: 31/62/125/250/500/1K/2K/4K/8K/16K Hz), `webAudioEq.ts` controller + `eqWorkletProcessor.ts` DSP (RBJ peaking from Audio EQ Cookbook), routed via captureStream → MediaStreamAudioSourceNode → AudioWorkletNode → GainNode → destination
- **EQ UI**: `EqualizerPanel.vue` (component) hosted by `EqualizerView.vue` (view), uses skin CSS variables
- **EQ + audio proxy (#1)**: KuGou CDN sends no CORS headers, so a local Tauri HTTP proxy (`audio_proxy.rs`, loopback 127.0.0.1) re-serves CDN media with CORS headers + range/resume, letting the EQ graph attach to cross-origin media. `eqState.available` exposed to UI; degradation banner shown only when the proxy is unavailable.
- **EQ graph build order (#4)**: full filter→gain→destination chain built BEFORE `captureStream` + `createMediaStreamSource` attach; throws are safe (element never gets stranded in a disconnected graph). The implementation **never** uses `createMediaElementSource`.
- **AudioContext lifecycle (#9)**: `webAudioEq.close()` releases context on teardown (HMR-safe). HMR preserves the `<audio>` element via `window.__bottlemusic_audio__` but closes the old AudioContext; the new module rebuilds the EQ graph via `initWebAudioEQ()`.
- **Degraded callback (#10)**: `AudioContext` creation failure, worklet load failure, or `enterDegradation()` fade-out surfaces via `onDegraded` (not `onSuspendedFail`); recovery via `onRecovered`.
- **PlaySessionTracker (state machine)**: sessions only open on real `play` event (no ghost sessions on rejected play()); `listened_seconds` is seek-immune (forward deltas 0<Δ<2s count, jumps/backward ignored); `completed` uses accumulator not duration; `setQuality` skip+intend keeps quality accurate
- **Event ownership (#2)**: `Html5AudioBackend.onEvent` is sole event source; `initPlayer` only handles `durationchange`/`loadedmetadata`. Double-`ended` handler that double-fetched `/song/url` is gone.
- **Single-loop replay**: handled in `ended` handler (not `next()`); `intend()` runs before `play()` (Bug A invariant)
- **Native MF playback / EchoPlayback\***: removed (architecture audit stage 2). Production EQ is **only** 10-band Web Audio.
- **BackendFacade**: removed; tests and production use CompatApi only.

## S5 Details

- **Schema**: `play_history_v2` table — song_hash, song_name, singer_name, album_id, album_name, cover_url, duration_seconds, completed, listened_seconds, quality, played_at. Indexed by `played_at` and `song_hash`.
- **Record path**: every play → 1 row. Tracked via `PlaySessionTracker` (skip-immune accumulator) + `stats_record_play` Rust command → `EchoStatsRecordPlay` C API → `PlayStatsService::RecordPlay` (SQL injection safety via `?N` parameter placeholders + identifier whitelist; no `SqlEscape` class exists)
- **Query endpoints** (5 `EchoStatsGet*` C API → 5 Rust query commands; combined with `EchoStatsRecordPlay` → `stats_record_play`, the stats module exposes 6 Rust Tauri commands total):
  - `stats_get_summary` — total plays, listened seconds, unique songs/artists, completion rate, per range (7d/30d/all)
  - `stats_get_top` — top N by song/artist/**album_id** (albums grouped by `album_id` not `name` to avoid same-name merges)
  - `stats_get_timeline` — daily play counts (`{date: "YYYY-MM-DD", count: N}`)
  - `stats_get_recent` — most recent N plays with full metadata (limit/offset)
  - `stats_get_recommendations` — "for you" based on top artists (local-only, no KuGou API fusion)
- **Thread safety**: `Ctx().stats` guarded by `shared_lock(Ctx().api_rwlock)`; `Database::Execute`/`ExecuteQuery` are public APIs that marshal work onto a single Actor thread via `Submit` (the Actor queue is protected by `Database::queue_mutex_`, not `mutex_`); all 5 query C API functions wrapped in try-catch with safe empty JSON fallback. `EchoShutdown` resets `Ctx().api`/`Ctx().stats`/`Ctx().db` pointers under the exclusive lifecycle lock.
- **AI analysis**: `ai_analyze` async Tauri command → reqwest → DeepSeek API. API key is held in memory only (`StatsView.vue` `aiApiKey = ref('')`, L54); legacy `localStorage.deepseek_api_key` is removed on module load (L53). Never logged, never persisted to disk or localStorage. 30s timeout. Chinese prompt, 200-word limit, covers listening habits + music taste + one interesting finding.
- **StatsView.vue**: 4 sections — overview cards (total plays / listened time / completion / unique counts), top lists with album art (song/artist/album), timeline CSS bar chart, recent plays list (with cover + completion badge), AI analysis panel

## Key Files

| File | Responsibility |
|---|---|
| `ui/src/playback/playerStore.ts` | Vue reactive player state + UI-facing commands; delegates playback transitions |
| `ui/src/playback/runtime/playbackOrchestrator.ts` | Playback transition orchestrator (Resolve + PlaySession + Backend sequencing) |
| `ui/src/playback/playSessionTracker.ts` | Stats session state machine + seek-immune accumulator |
| `ui/src/playback/eq/webAudioEq.ts` | Web Audio API AudioWorklet EQ graph controller (proxy-enabled, safe build order) |
| `ui/src/playback/runtime/playerBackend.ts` | Tauri invoke wrapper with S1 resilience |
| `ui/src/app/appearance/themeStore.ts` | Skin/mode management |
| `ui/src/platform/tauri/circuitBreaker.ts` | Frontend resilience (CircuitBreaker) |
| `ui/src/playback/data/recentPlayedStore.ts` | Local-first recent-played store (localStorage, FileHash dedupe, mergeRemote) |
| `ui/src/features/lyrics/useLyricFollow.ts` | Lyric auto-follow state machine composable |
| `ui/src/playback/playbackDiagnostics.ts` | Playback boundary event ring buffer (diagnostics) |
| `ui/src/components/EqualizerPanel.vue` | EQ UI (10 sliders, 6 presets, degradation banner) |
| `ui/src/views/EqualizerView.vue` | Equalizer view (EQ panel host) |
| `ui/src/views/StatsView.vue` | Statistics dashboard (overview + top + timeline + recent + AI) |
| `ui/src-tauri/src/backend_api.rs` | CApiHandle, DLL loading, event bridge |
| `ui/src-tauri/src/stats.rs` | 6 Tauri stats commands |
| `ui/src-tauri/src/ai_analysis.rs` | DeepSeek AI analysis async command |
| `ui/src-tauri/src/audio_proxy.rs` | Local HTTP proxy for cross-origin CDN media (CORS, range/resume, SSRF allowlist) |
| `ui/src/platform/tauri/audioProxy.ts` | Frontend wrapper for audio_proxy_url Tauri command |
| `native/core/C_API.cpp` | C API exports, Ctx().api, Ctx().scheduler, Ctx().stats, Ctx().db (EchoContext Meyers singleton; no g_playback — MFS removed) |
| `native/core/HttpClient.cpp` | WinHTTP + watchdog + retry budget |
| `native/async/RequestScheduler.cpp` | Thread pool + bounded shutdown/restart |
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

# Rust tests (--no-default-features: tray-icon/global-shortcut crash the lib test harness)
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib --no-default-features -- --test-threads=1

# Frontend tests
cd C:\BottleMusic\ui && pnpm test -- --run
```

## Test Counts

测试数量随代码变化，不作为长期架构事实记录。当前基线见 [docs/wiki/evidence-report.md](./docs/wiki/evidence-report.md) 的「测试统计」一节。

## Known Issues

1. **MFS native playback removed** — Media Foundation playback stack was removed on 2026-07-17 (architecture audit stage 2). `native/playback/` directory no longer exists; production playback is HTML5 Audio + Web Audio API EQ only. See [docs/wiki/playback-runtime.md](./docs/wiki/playback-runtime.md).
2. **EQ for KuGou CDN (RESOLVED)** — the local audio proxy (`audio_proxy.rs`) re-serves cross-origin CDN media with CORS headers + range/resume, so the EQ graph attaches. Degradation banner shows only when the proxy is unavailable.
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

**PlaybackOrchestrator**:
The module that owns playback transitions and the ordering between Resolve, PlaySession, and Backend.
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
The Web Audio API AudioWorklet graph (10 bands), routed through the local audio proxy so cross-origin CDN media can be EQ'd.
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
- **docs/ tracking** — `docs/wiki/` and `docs/adr/` are trackable; `docs/superpowers/`, `docs/captures/`, `docs/tmp/` remain gitignored. See [.gitignore](./.gitignore).

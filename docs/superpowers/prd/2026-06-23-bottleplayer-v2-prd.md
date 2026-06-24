# PRD: BottleMusic v2.0.0

**Status:** ready-for-agent
**Date:** 2026-06-23
**Owner:** Ningbottle
**Supersedes:** v1.0.0 (released 2026-06-04)

> **Implementation Status (updated 2026-06-24):**
> - S1 ✅ Complete — 3-layer deadline, CircuitBreaker, bounded Shutdown, HttpClient watchdog
> - S2 ✅ Complete — CI, release workflow, sync-version, skip-version
> - S3 ✅ Complete — themeStore, Aurora + Newsprint skins, dark mode, FOUC prevention
> - S4 ⚠️ Partial — HTML5 playback + Web Audio API EQ (working). MFS native path abandoned (deadlock + topology issues). EqualizerMFT code exists but unused.
> - S5 ❌ Not started — Play count, listening history, dashboard

## Problem Statement

BottleMusic v1.0.0 ships a working Tauri 2.0 + Vue 3 + C++ unofficial KuGou Concept Edition client, but it has three problems that block it from being a reliable daily driver:

1. **It freezes on unstable networks.** When the network blips, the whole app becomes unresponsive — spinners spin forever, and even clicking the window close button hangs the process. Users have no way to recover short of killing it from Task Manager. This is the most reported v1 complaint.

2. **It looks like one fixed skin.** The Newsprint newspaper aesthetic is the only look. Users who want a different visual identity have no way to get one, and the theme engine under the hood is half-built (a runtime variable override mechanism exists but there is no skin registry, no Settings appearance section, and dark mode is hardcoded twice).

3. **It plays through the WebView.** Audio decoding lives in the HTML5 Audio element inside the WebView, which means no native DSP pipeline, no equalizer, no audio effects, and no resilient native playback. A fully-implemented C++ Media Foundation PlaybackController already exists in the source tree but is not linked into the shipped DLL and has zero FFI exports.

On top of these, v2 wants to ship two net-new capability areas: **play statistics and recommendations** (the `play_history` SQLite table exists but is dead — never written, never read), and **automated release + auto-update CI** (the client-side updater is 100% wired, but there is zero CI and releases are built by hand).

## Solution

Ship BottleMusic v2.0.0 as a phased major update, structured as six independently-shippable sub-projects on a shared FFI boundary, delivered in three phases:

**Phase 1 — Stabilize and Automate (S1 + S2)**
Fix the freeze root causes (end-to-end deadlines, bounded shutdown, frontend resilience), and stand up CI that builds, tests, signs, and publishes releases automatically. End result: v2 ships on a foundation that does not hang and can be released by pushing a tag.

**Phase 2 — Visible v2 (S3 + S4)**
Ship a new skin (the existing Newsprint becomes one of two peer skins, selected from a new Settings appearance section) and wire up the C++ Media Foundation playback core end-to-end, including the pipeline rewrite from the high-level MFPlay API to a full `IMFMediaSession` topology with a custom DSP `IMFTransform` so a real equalizer can be inserted into the audio pipeline. End result: v2 looks new and plays through native audio with an EQ.

**Phase 3 — Data and Polish (S5)**
Bring the dead `play_history` table to life with a richer schema, emit play events from the player (including completion and skip detection), build a stats dashboard and a local recommendation engine that fuses local play history with the already-wired KuGou endpoints. End result: v2 tells users what they listen to and recommends new music.

The three phases ship as v2.0.0 (Phase 1+2 together as the major release), then v2.1.0 (Phase 3). Each sub-project has its own spec and implementation plan; this PRD is the umbrella.

## User Stories

### Resilience / Anti-Freeze (S1)

1. As a listener on flaky Wi-Fi, I want the app to show a "network degraded" banner and keep responding to clicks while requests are retrying, so that it never looks frozen.
2. As a listener, I want any single backend request to time out within 15 seconds, so a hung request never blocks the UI indefinitely.
3. As a listener, I want failed idempotent requests (song URL resolve, cover fetch, home feed) to retry with exponential backoff, so a momentary blip heals itself without me doing anything.
4. As a listener, I want the app to show a circuit-breaker "offline" state after several consecutive failures, so it stops stacking new requests and degrades to cached/empty content instead of piling up spinners.
5. As a listener, I want clicking the window close button to always close the app within 5 seconds even if a request is hung, so I never have to use Task Manager.
6. As a listener on the login QR screen, I want the QR poll to slow down when the network is bad, so it does not stack a dozen overlapping hung polls.
7. As a listener, I want the home page to render sections independently, so one slow endpoint does not block the whole page from showing.
8. As a listener, I want the app to recover automatically when the network comes back, so I do not have to restart it.

### Auto-Update / CI (S2)

9. As a user, I want the app to check for updates on startup and show a badge in the sidebar when one is available, so I know to upgrade. *(Already works in v1 — preserve.)*
10. As a user, I want to click "Download and install" in Settings and see a progress bar, so I know the update is happening. *(Already works in v1 — preserve.)*
11. As a user, I want the app to relaunch itself after an update installs, so I do not have to remember to restart manually.
12. As a user, I want to skip a specific update version, so I am not nagged about a version I do not want.
13. As a maintainer, I want every push and pull request to run Vitest, CTest, vue-tsc, and cargo clippy in CI, so regressions are caught before merge.
14. As a maintainer, I want pushing a `v*` tag to build a signed Windows installer (NSIS + MSI), generate `latest.json`, and upload everything to the GitHub Release, so users get the update automatically.
15. As a maintainer, I want the release workflow to build the C++ DLL in the Release preset, so the shipped binary is not a Debug build.
16. As a maintainer, I want the signing private key to come from a GitHub secret, not a developer's laptop, so releases are reproducible by anyone with repo access.
17. As a maintainer, I want a single source of truth for the version number, so the five currently-handcoded version strings cannot drift.

### Skin System (S3)

18. As a user, I want the existing Newsprint look to remain available as a named skin, so the v1 aesthetic I am used to is not taken away.
19. As a user, I want to pick a second, new v2 skin from a Settings "Appearance" section, so I can give the app a fresh look.
20. As a user, I want light and dark mode to be independent of which skin I pick, so I can combine any skin with any mode.
21. As a user, I want my skin and mode choice to persist across restarts, so I do not have to re-pick it every launch.
22. As a user, I want the app to open in my chosen skin with no flash of the wrong theme, so the first paint is already correct.
23. As a user, I want the existing "Tweaks" drawer (accent color, warmth, grain, blur, compact, lyric alignment, custom wallpaper) to keep working on top of either skin, so I do not lose my fine-tuning.
24. As a user, I want the lyric view alignment and the sidebar/playlist/search/lyric views to all respect my skin, so the look is consistent everywhere.

### C++ Playback Core + Equalizer (S4)

25. As a listener, I want playback to go through the native C++ Media Foundation pipeline, so audio is decoded and rendered by the OS audio stack instead of the WebView.
26. As a listener, I want play, pause, resume, stop, seek, volume, and rate controls to work exactly as they do today, so switching to native playback is invisible to my habits.
27. As a listener, I want the song queue, shuffle, single-loop, and list-loop modes to keep working, so my queue behavior does not regress.
28. As a listener, I want lyrics to stay in sync when playback moves to native, so the lyric highlight and auto-scroll keep working.
29. As a listener, I want the app to fall back to HTML5 Audio if native playback fails to initialize, so I am never left with no audio at all.
30. As a listener, I want an equalizer in the player with at least 5 frequency bands, so I can shape the sound to my taste.
31. As a listener, I want EQ presets (e.g. Flat, Bass Boost, Vocal, Rock) and a custom mode, so I do not have to tune bands by hand if I do not want to.
32. As a listener, I want my EQ settings to persist across restarts, so I do not re-tune every launch.
33. As a listener, I want the EQ to be toggleable on/off without losing my band settings, so I can A/B compare.
34. As a listener, I want a spectrum/level visualizer in the player that reflects the playing audio, so the EQ has visual feedback. *(Stretch — requires tapping the MF pipeline.)*

### Stats / Recommendations (S5, ships as v2.1.0)

35. As a listener, I want my plays to be recorded locally even when I am logged out, so my stats are not gated on KuGou login.
36. As a listener, I want a play to be recorded with a completion percentage, so a 5-second skip is not counted the same as a full listen.
37. As a listener, I want the stats dashboard to show my top artists, albums, and songs over 7-day, 30-day, and all-time ranges, so I can see what I actually listen to.
38. As a listener, I want the stats dashboard to show my total listening time and a play timeline, so I can see my listening habits over time.
39. As a listener, I want a "For You" recommendations section that blends my local play history with KuGou's daily recommendations and ranks, so I get suggestions that reflect both my taste and what is trending.
40. As a listener, I want "more from this artist" and "more from this album" links from the now-playing track, so I can discover related music in one click.
41. As a listener, I want the existing History view (KuGou server-side history) to remain available alongside the new local stats, so I do not lose the v1 history feature.

## Implementation Decisions

### Architecture

- **Six sub-projects, one shared FFI boundary.** S1–S5 each get their own spec and implementation plan. S6 (equalizer) is folded into S4 because the EQ requires the same MFPlay → IMFMediaSession pipeline rewrite that S4 needs for native playback. Dependencies: S1 and S2 are independent; S3 is independent; S4 depends on S1 (shared FFI boundary discipline); S5 depends partially on S4 (play events come from the player).
- **Phased delivery.** Phase 1 (S1 + S2) and Phase 2 (S3 + S4) ship together as v2.0.0. Phase 3 (S5) ships as v2.1.0. The phases correspond to "stabilize", "visible v2", and "data".
- **Strategy A (stabilize first).** Resilience and CI land before any new feature work, so v2 is built on a non-freezing, releasable foundation.

### S1 — Resilience / Anti-Freeze

- **End-to-end deadline on every backend call.** Enable the tokio `time` feature in the Rust crate and wrap the `spawn_blocking` C++ call in `tokio::time::timeout` (default 15s, configurable per route). A timeout returns a structured error the frontend can degrade on. This is the single highest-leverage fix.
- **Bounded, non-blocking shutdown.** Both the Rust `shutdown_c_api` write-guard acquisition and the C++ `EchoShutdown` exclusive-lock acquisition switch to timed acquisition (`try_write` / `try_lock_for`). If in-flight calls do not drain within N seconds, force-close the WinHTTP connection pool and proceed anyway. Eliminates the close-window freeze.
- **Frontend resilience layer.** `backend.ts` wraps `invoke('native_request')` with a timeout, adds bounded exponential-backoff retry for idempotent GETs, and a simple circuit breaker that short-circuits after N consecutive failures so the UI degrades to cached/empty state. The existing `backendHealth` function (currently unused) becomes the reconnect indicator.
- **Login QR poll fix.** Gate the 2-second `setInterval` on the previous call completing, add error backoff, and stop polling after K consecutive failures.
- **RwLock poison recovery.** Replace `.unwrap()` on the RwLock with `.unwrap_or_else(|p| p.into_inner())` so a poisoned lock does not kill every subsequent backend call.
- **WinHTTP hardening.** Add a per-request total receive deadline, a total body-read deadline with max-size guard in the read loop, TCP keepalive on connect handles, and bounded retry with backoff in `ExecuteRequest`. Keep the existing connection pool.
- **Decide the fate of the dead `RequestScheduler`.** Either wire it into the production path with per-job deadlines and a timeout on `Shutdown` join, or delete it. As-is it gives a false sense of bounded concurrency.
- **Home view per-section rendering.** Give each home feed section its own loading state and timeout so one slow endpoint does not block the page.
- **Global network-degraded banner.** Add an `App.vue`-level banner driven by the circuit-breaker state.

### S2 — Auto-Update / CI

- **CI workflow on push/PR.** `.github/workflows/ci.yml` runs Vitest, CTest (via `cmake --preset bottlemusic-check` + `ctest`), `vue-tsc --noEmit`, and `cargo clippy`/`cargo fmt --check`. Sets up MSVC (VsDevCmd), vcpkg manifest mode, Rust toolchain, pnpm with cache.
- **Release workflow on `v*` tags.** `.github/workflows/release.yml` builds the C++ DLL in the Release preset, runs `pnpm tauri build` with `TAURI_SIGNING_PRIVATE_KEY`/`_PASSWORD` from GitHub secrets, and uploads NSIS + MSI + `.sig` + `latest.json` to the GitHub Release. Uses `tauri-apps/tauri-action` for the sign+upload+latest.json step.
- **Fix `build-backend.ps1`.** Accept a `-Preset`/`-Config` parameter so it can build `bottlemusic-release` for packaging, not just Debug.
- **Post-install relaunch.** Replace the "please restart manually" message in `SettingsView.vue` with a `relaunch()` call via the Tauri process plugin.
- **Update UX additions.** Add a "skip this version" choice, an update-channel toggle (stable/pre-release), and surface update-available on the Home view in addition to the sidebar badge.
- **Single source of truth for version.** Centralize the version string (e.g. a `version.json` or `tauri.conf.json` as source, injected into `package.json`/`Cargo.toml`/`CMakeLists.txt`/`vcpkg.json` at build time) so the five currently-handcoded values cannot drift.

### S3 — Skin System

- **Existing Newsprint becomes one peer skin.** The current `:root` tokens become `:root[data-skin="newsprint"]`. A new v2 skin (visual direction to be locked in a separate `reference-design-contract` design pass) becomes `:root[data-skin="v2"]`. Light/dark becomes an orthogonal `data-mode` attribute.
- **Token purity.** Extract every hardcoded color/radius/shadow in `style.css` and the component `<style scoped>` blocks into tokens, including the ~130 lines of `html.dark` rgba overrides. Fix the three known undefined-token leaks (`--paper-alt`, `--ink-light`, `--ink-soft-10`).
- **Skin registry.** A `skins.ts` module exports named skin definitions (`{ id, label, tokens, supportsDark }`). `Drawer.vue`'s `applyTweaks` is refactored from "sets 15 individual properties" to `applySkin(skinId, mode)` plus the existing modulators (warmth/blur/grain/accent/bg/compact/lyric-align) which stay as overlays on the base skin.
- **Reactive theme store.** A new `themeStore.ts` (mirroring the `playerStore`/`userStore` pattern) holds skin state reactively, killing the 500ms localStorage polling in `LyricView.vue`.
- **FOUC prevention.** An inline `<script>` in `index.html` reads `tweak_skin`/`tweak_mode` from localStorage and sets `document.documentElement.dataset.skin`/`dataset.mode` synchronously before `main.ts` mounts.
- **Settings appearance section.** Add an "Appearance / 外观" section to `SettingsView.vue` with skin picker, mode toggle, and the existing modulators.
- **Background-layer decoupling.** The four Newsprint procedural background layers in `App.vue` become skin-aware (gated behind `:root[data-skin="newsprint"]`) so the new skin can ship its own background.
- **Visual direction locked via design skills.** The new skin's look is defined by a `reference-design-contract` → `design-md` pass before implementation, reviewed through `plan-design-review` and polished with `impeccable-design-polish`.

### S4 — C++ Playback Core + Equalizer (largest sub-project)

- **PlaybackController pipeline rewrite.** Replace the current MFPlay-based `PlaybackController` (high-level `MFPCreateMediaPlayer`, which hides the topology and cannot accept DSP) with an `IMFMediaSession` + explicit `IMFTopology` pipeline: Source → Decoder → **EQ MFT (custom `IMFTransform`)** → Audio Renderer. The existing `PlaybackController` public ABI (`PlayUrl`, `Pause`, `Resume`, `Stop`, `Seek`, `SetVolume`, `SetRate`, `GetState`) is preserved so the existing C++ contract tests at `basic_contract_tests.cpp:942-968` keep passing.
- **Custom EQ MFT.** A biquad/IIR equalizer `IMFTransform` with at least 5 bands. C API: `EchoPlaybackSetEqEnabled`, `EchoPlaybackSetEqBand`, `EchoPlaybackSetEqBands`, `EchoPlaybackGetEqBands`. Presets (Flat/Bass Boost/Vocal/Rock) live on the frontend and call `EchoPlaybackSetEqBands`.
- **Link EchoPlayback into EchoCAPI.dll.** `CMakeLists.txt` adds `EchoPlayback` to the `EchoCAPI` link list (currently links only `EchoCore`), bringing the `mf*` libs transitively.
- **C API playback exports.** New `EchoPlaybackInitialize`, `EchoPlaybackPlayUrl`, `EchoPlaybackPause`, `EchoPlaybackResume`, `EchoPlaybackStop`, `EchoPlaybackSeek`, `EchoPlaybackSetVolume`, `EchoPlaybackSetRate`, `EchoPlaybackGetState`, `EchoPlaybackShutdown`. A process-global `PlaybackController*` in `C_API.cpp`, guarded by the existing `g_api_rwlock` or a dedicated mutex.
- **Event channel wiring.** The existing no-op `EchoSetEventCallback` gets a real body. `PlaybackController::HandleMediaEvent` serializes state events to JSON and invokes the callback; the Rust FFI registers an `ffi_event_callback` that emits to the Tauri `AppHandle` (`app.emit("playback_event", payload)`).
- **Rust bindings.** `CApiHandle` in `backend_api.rs` is extended with playback fn pointers + `EchoSetEventCallback` registration in `init_with_paths`.
- **Tauri commands.** `playback_play_url`, `playback_pause`, `playback_resume`, `playback_stop`, `playback_seek`, `playback_set_volume`, `playback_set_rate`, `playback_get_state`, `playback_set_eq_enabled`, `playback_set_eq_bands` registered in `invoke_handler`.
- **Frontend player abstraction.** A `PlayerBackend` interface with two implementations: `Html5AudioBackend` (wraps the existing `Audio` element, kept as fallback) and `NativePlaybackBackend` (calls `invoke('playback_*')` and `listen('playback_event')`). `playerStore` actions delegate to the active backend; `isPlaying`/`currentTime`/`duration` are updated from `playback_event` emissions (native) or `timeupdate` (HTML5).
- **Audio data flow.** The URL resolved by `/song/url` is passed to `EchoPlaybackPlayUrl`; MFPlay/MFSession pulls and decodes the HTTP stream itself. No raw audio bytes cross the FFI boundary.
- **Lyrics position source.** `playerStore.currentTime` is fed from native position events (or polling `EchoPlaybackGetState` at ~4-10 Hz) instead of HTML5 `timeupdate`. `LyricView.vue` highlight logic is unchanged since it reads `playerStore.currentTime`.
- **Diagnostics.** The hardcoded `"webview"` in `DiagnosticsRoutes.cpp` is replaced with the real native playback state string.
- **EQ UI.** A new equalizer panel (slider per band, preset dropdown, enable toggle) in the player area, persisted via the theme/player store pattern.

### S5 — Stats / Recommendations (v2.1.0)

- **Richer `play_history` schema.** Migrate the dead `play_history` table (currently `mix_song_id, played_at, progress_seconds`) to add: `song_hash, song_name, singer_name, album_id, album_name, duration_seconds, completed, listened_seconds, source, quality, user_id, played_at_ms`. Bump `user_version` and add an `ALTER TABLE` migration path (the current `CREATE TABLE IF NOT EXISTS` does not migrate existing tables).
- **Local play-event writer.** A new `PlayStatsService` + a `HandlePlayRecord` route, called from `playerStore` on play-start and on `ended`/skip. Emits completion (`completed=1` vs `0`) and `listened_seconds = audio.currentTime` (or native position).
- **Skip detection.** `next()`/`prev()`/`playTrack(other)` in `playerStore` emit a skip event with the listened fraction before switching.
- **Stats query layer.** A `StatsRepository` + routes (`/stats/summary`, `/stats/top?dim=artist|album|song&range=7d|30d|all`, `/stats/timeline`) over a generic prepared-statement query method added to `Database`, or a read-only second connection (WAL already allows concurrent readers).
- **Aggregation tables (optional).** Precomputed `play_counts`/`artist_stats`/`album_stats` to avoid scanning a growing event log on every dashboard load.
- **RecommendationService.** Fuses local top artists/albums (from `play_history`) with the already-wired KuGou endpoints (`everyday_song_recommend`, ranks, tag/specialList, `CatalogService` artist/album related content). New routes `/recommend/songs`, `/recommend/for-you`.
- **Stats dashboard view.** A new view (replacing or extending `HistoryView.vue`) with top artists/albums/songs, listening time, play timeline, and a "For You" recommendations section. Uses the `data-report` and `d3-visualization` skills for the UI.
- **DB filename duality.** Schema migrations must run in both the production `bottlemusic.db` (via `app_data_dir`) and the fallback `echomusic-native.db`. `Initialize()` already re-runs `InitializeSchema` idempotently, but a real migration step is needed.

## Testing Decisions

### Seams (two, both pre-existing — no new seams introduced)

1. **Vitest + jsdom (frontend, highest seam).** Prior art: `ui/src/api/__tests__/vipResolver.test.ts`, `favorite.test.ts`, `trackInfo.test.ts`. Tests exercise the public `apiGet`/`apiPost`/store API, never internal implementation. v2 adds: mocked `invoke` to verify timeout/retry/circuit-breaker behavior (S1); mounted components to verify `data-skin` token application (S3); a mocked `PlayerBackend` to verify `playerStore` drives either backend (S4); mocked `apiGet('/stats/*')` to verify the dashboard (S5).
2. **CTest contract tests (C++, highest seam).** Prior art: `native/tests/basic_contract_tests.cpp` (1699 lines, includes the existing `PlaybackController` contract test at `:942-968`), plus `route_contract_test.cpp`, `songurl_contract_test.cpp`, `playlist_contract_test.cpp`, `profile_signature_contract_test.cpp`, `home_contract_test.cpp`. v2 adds: a slow-server fixture to verify `HttpClient` total deadline (S1); extended `PlaybackController` contract tests for the new IMFMediaSession pipeline + EQ MFT frequency response (S4); `PlayStatsService` contract tests (S5).

### What makes a good test here

- Tests verify behavior through the public interface (`apiGet`/`apiPost` for frontend, the C API exports + compat routes for C++), never internal implementation. A test that breaks when an internal function is renamed is a bad test.
- No mocking of internal collaborators. The frontend mocks only the Tauri `invoke` boundary; the C++ tests use real services against fixtures (and a real Windows audio device for playback contract tests, as the existing tests already do).
- Each test reads like a specification: "a hung WinHTTP call returns within 15 seconds", "the EQ MFT attenuates band N by -6dB", "a play event is recorded with completion=1 when the track ends naturally".
- **TDD vertical slices.** Per the test-driven-development skill: one test → one implementation → repeat. Never write all tests first then all implementation. Each test responds to what the previous cycle revealed.

### Visual regression (S3, not automated)

- The skin system has no automated visual regression seam. Instead, `plan-design-review` and `design-review` are used as human review gates before merge, and `impeccable-design-polish` for the final pass. This is an explicit decision: E2E/Playwright would be a third seam and is out of scope for v2.

### Prior art referenced

- `vipResolver.test.ts` — pure-logic resolver tested through its public `resolveVip(data, nowMs)` function. Model for S5 stats logic tests.
- `favorite.test.ts` — normalizer tested through its public `normalizePlaylists` function. Model for S3 skin token tests.
- `basic_contract_tests.cpp:942-968` — PlaybackController exercised through its public ABI against a real Windows WAV. Direct model for S4 extension.

## Out of Scope

- **Multi-arch builds (arm64).** v2 is x64-only; arm64 is a future consideration.
- **macOS / Linux.** BottleMusic is Windows-only by architecture (Media Foundation, WinHTTP, MSVC); cross-platform is not in v2.
- **Offline mode / downloaded music.** Local file playback and download-for-offline are not in v2.
- **Lyrics rewrite.** The C++ `LyricParser` and the frontend `parseLrc` are redundant, but unifying them is an optimization, not a v2 requirement. Lyrics only need the position-source swap (HTML5 `timeupdate` → native events).
- **Native image cache wiring.** The built-but-unwired `ImageLoader`/`DiskImageCache` is not wired in v2; cover bytes keep going through the WebView `<img>` stack. (The `/images/audio` URL-resolution fetch does get resilience fixes via S1.)
- **Deleting `BackendFacade` or `cpp-httplib`.** S1 decides whether to wire or delete the dead `RequestScheduler`; `BackendFacade` and `cpp-httplib` cleanup is deferred unless S1 chooses to adopt them.
- **Automated visual regression / Playwright.** A third test seam is not introduced; skin quality is enforced by human design-review gates.
- **S6 as a separate sub-project.** The equalizer is folded into S4 because it requires the same pipeline rewrite.

## Further Notes

- **Sub-project specs.** Each of S1–S5 gets its own design spec under `docs/superpowers/specs/` and its own implementation plan under `docs/superpowers/plans/`, produced by the `brainstorming` → `writing-plans` flow. This PRD is the umbrella; the specs contain the per-sub-project detail.
- **Skills used across v2.** superpowers (`brainstorming`, `writing-plans`, `tdd`, `subagent-driven-development`, `dispatching-parallel-agents`, `systematic-debugging`, `verification-before-completion`, `requesting-code-review`, `finishing-a-development-branch`); Open Design (`reference-design-contract`, `design-md`, `design-taste-frontend`, `plan-design-review`, `design-review`, `impeccable-design-polish`, `stitch-loop`, `data-report`, `d3-visualization`, `copywriting`).
- **Dead code surfaced by exploration.** v2 exploration confirmed: `RequestScheduler` (built, never wired into production), `BackendFacade` (built, never wired into CompatApi), `ImageLoader`/`DiskImageCache` (built, only used in tests), `play_history` and `image_cache` SQLite tables (defined, never read or written), `cpp-httplib` vcpkg dependency (listed, never imported). S1 and S5 resolve the relevant ones; the rest are noted for future cleanup.
- **v1.0.0 shipped with zero CI.** The two GitHub releases were built manually on a developer's Windows machine. v2 changes this via S2.
- **Version sync at v1.0.0 is clean.** All five version strings (`ui/package.json`, `tauri.conf.json`, `Cargo.toml`, `CMakeLists.txt`, `vcpkg.json`) are 1.0.0. v2 bumps to 2.0.0 (Phase 1+2) and 2.1.0 (Phase 3) with the S2 single-source-of-truth mechanism.

# BottleMusic Product Stability Design

Date: 2026-07-03

## Goal

Move BottleMusic from a usable music player into a more complete daily-use product by stabilizing the listening loop. This phase does not prepare a public release, installer, or large visual redesign. It focuses on the problems users already feel while listening: playback continuity, recent-played consistency, lyric tracking, and enough diagnostics to investigate stalls without guessing.

## Current Context

BottleMusic is a Tauri 2 desktop app with a Vue 3 frontend and a native C++ compatibility API. Recent work has focused on playback reliability, audio proxy resume, HTML5 playback diagnostics, EQ degradation behavior, and personal FM recommendation continuation.

The current working set already includes changes for personal FM continuation and automatic next-track protection. This design treats those fixes as part of the stability baseline and defines the next productization layer around them.

## Scope

This phase includes:

- Playback stability and stall diagnostics.
- Recent-played synchronization and local fallback behavior.
- Lyric auto-follow behavior.
- Small product-quality states directly related to these flows, such as empty states, retry affordances, and a diagnostics entry point.

This phase excludes:

- Public release packaging, installer work, update channels, or GitHub release automation.
- A full visual redesign of the application.
- New music discovery surfaces unrelated to the stability loop.
- Re-enabling native Media Foundation playback as the default path.

## Approach Options

### Option A: Bug-Only Fixes

Fix recent-played sync, lyric auto-follow, and known playback continuation bugs directly.

Trade-off: fastest path, but future stalls may remain hard to diagnose.

### Option B: Stability Plus Diagnostics

Fix the three user-facing bugs and add a small diagnostics layer that records useful playback evidence when stalls happen.

Trade-off: slightly more work, but it turns vague reports such as "it got stuck" into actionable evidence.

### Option C: Broad Product Polish

Fix the bugs and polish broader UI surfaces, including settings, empty states, layout, and navigation.

Trade-off: improves product feel, but risks diluting the stability work.

## Recommendation

Use Option B. BottleMusic should first become dependable in the core listening loop. Diagnostics are important because playback stalls can originate from several layers: KuGou URL resolution, the audio proxy, CDN range streaming, HTML5 media events, Web Audio EQ behavior, or player state transitions. A small diagnostic layer lets the team distinguish these causes before attempting future fixes.

## User-Facing Requirements

### Playback Stability

- A personal FM queue must continue beyond the initial daily recommendation seed.
- Natural track endings must not skip the first newly appended recommendation.
- If personal FM recommendation fetching fails transiently, the player should retry before giving up.
- If playback stalls, the app should record enough evidence to identify whether the issue came from media events, audio proxy transfer, URL resolution, recommendation fetch, or state-machine transition.
- Playback errors should not leave the user trapped on a bad track when another playable item is available.

### Recent Played

- When a track begins successfully, the recent-played list should update locally without waiting for remote upload.
- Uploading play history to KuGou should remain best-effort and must not interrupt playback.
- The recent-played view should merge local recent records with remote history when available.
- If remote history fails or lags, the user should still see the local recently played tracks.
- Duplicate records for the same track should collapse to the latest play time.

### Lyrics

- Opening the lyric view should automatically follow the current lyric line.
- The user should not need to manually scroll before lyric tracking starts.
- Manual scrolling should temporarily suspend auto-follow so the user can inspect earlier lyrics.
- Auto-follow should resume after a short idle period or through a visible "return to current" affordance.
- Switching tracks should reset lyric tracking state so previous scroll position does not affect the new track.

### Product Quality States

- Recent-played empty and error states should explain the state without blaming the user.
- Lyrics should show clear states for loading, unavailable lyrics, and sync mismatch.
- A diagnostics entry point should expose recent playback events in a compact, copyable form for debugging.

## Architecture

### Frontend State

The player store remains the owner of playback state and queue transitions. It should also emit compact playback diagnostic events when important boundaries occur:

- Track switch requested.
- Song URL resolution started, succeeded, or failed.
- HTML5 media event observed: `waiting`, `stalled`, `suspend`, `error`, `ended`.
- Audio proxy URL prepared.
- Personal FM fetch started, succeeded, returned no fresh songs, or failed.

Recent-played local state should be a small dedicated store or module instead of being hidden inside the player store. It should accept normalized tracks and timestamps, persist to local storage or the existing native storage layer, and expose a merge function for remote history.

Lyric tracking should be represented as explicit state:

- `autoFollowing`: whether the view should follow the current line.
- `manualScrollUntil`: a timestamp until which auto-follow is suppressed.
- `activeLineId` or active line index derived from playback time.
- `trackKey`: used to reset tracking when the song changes.

### Native Backend

The native backend already exposes song URL, history upload, lyrics, and diagnostics routes. This phase should avoid large native reshaping. Native work should be limited to:

- Preserving useful error messages from `/playhistory/upload`, `/song/url`, and audio proxy failures.
- Exposing any already available playback or proxy diagnostics through an existing diagnostics route when practical.
- Keeping contract tests for route shape and error handling.

### Data Flow

1. A track starts through the player store and playback orchestrator.
2. Song URL resolution succeeds.
3. The HTML5 backend starts playback.
4. The player records local recent-played immediately after playback succeeds.
5. The player uploads remote play history best-effort in the background.
6. The recent-played view loads local entries first, then remote history, then merges and sorts them.
7. The lyric view derives the active line from `currentTime` and scrolls only when auto-follow is active.
8. Playback diagnostic events are recorded at each important boundary and can be inspected when a stall occurs.

## Error Handling

- Remote history upload failure should be logged and surfaced only as a non-blocking sync status.
- Remote recent-history fetch failure should not hide local recent records.
- Lyric fetch failure should show a stable unavailable state, not an empty broken panel.
- Personal FM fetch failure should stop retrying after a small bounded retry sequence and should not loop the old seed list.
- Playback stalls should be diagnosed through event collection before adding more recovery behavior.

## Testing Strategy

### Unit and Component Tests

- Recent-played local insert, dedupe, sort, persistence, and remote merge.
- Recent-played view fallback when remote history fails.
- Lyric auto-follow starts on first render without manual scrolling.
- Lyric manual scroll suppresses auto-follow and then resumes.
- Lyric state resets on track change.
- Player store does not double-advance when duplicate `ended` events arrive during an in-flight personal FM transition.

### Native Contract Tests

- History upload route preserves status and error information.
- Diagnostic route shape remains stable if new diagnostic data is exposed.

### Manual Verification

- Play at least 30 minutes through personal FM.
- Confirm newly appended recommendations begin from the first fresh track.
- Confirm recent-played updates immediately after a successful track start.
- Confirm lyrics follow automatically when opening the lyric view.
- Trigger or wait for a stall and verify diagnostic output includes track, position, media event, URL-resolution status, and proxy hints when available.

## Acceptance Criteria

- Personal FM and automatic next-track playback do not skip the first newly appended recommendation.
- Recent-played shows the latest successful local plays even when remote sync is delayed or unavailable.
- Lyrics auto-follow from the moment the lyric view opens, without requiring the user to scroll first.
- Manual lyric scrolling feels intentional and does not fight the user.
- A playback stall report can be investigated using captured diagnostics rather than only user memory.
- Full frontend tests and relevant native contract tests pass for the implemented changes.

## Implementation Phases

### Phase 1: Recent Played Local-First

Create or refine a recent-played local store, write successful playback entries immediately, merge remote history when available, and update the recent-played view.

### Phase 2: Lyric Follow State Machine

Make lyric tracking explicit, handle manual scroll suppression, add return-to-current behavior, and reset state on track changes.

### Phase 3: Playback Diagnostics

Collect playback boundary events and expose a compact diagnostics view or settings entry. Use this to investigate stalls before making deeper audio pipeline changes.

### Phase 4: Stability Review

Review the personal FM and automatic next-track fixes together with the new diagnostics. Run focused manual listening sessions and turn any reproduced stall into a separate root-cause debugging task.

## Decisions (resolved 2026-07-03 grilling)

The two open decisions below plus six additional design questions were resolved through a grilling session on 2026-07-03. Resolutions are binding for implementation. Original open questions are kept as context, each followed by its resolution.

1. **Baseline commit** (prerequisite). Commit the 14 uncommitted working-tree files (personal FM continuation + next-track protection, +485/-34 lines across C++ `HomeService`/`MediaRoutes` and frontend `playerStore`/`HomeView` + tests) as one commit on `main` before starting Phase 1, so Phase 1 diff stays clean and bisectable. The `playback-orchestrator-tdd` worktree branch is already merged (commit `e1087af6` is in main's history) — no reconciliation needed.
2. **CONTEXT.md staleness** (done). Fixed 11 points: EQ is AudioWorklet 10-band (31/62/125/250/500/1K/2K/4K/8K/16K Hz) not BiquadFilterNode 5-band; `audio_proxy.rs` resolves known issue #2 (CORS-gated EQ); Rust test count is 22 not 11; `audio_proxy.rs` added to architecture diagram + key files; EQ graph language term updated.
3. **Phase scope/order**. Option B (Stability Plus Diagnostics), phases in spec order 1→2→3→4. Phase 3→4 has a dependency (review needs diagnostics); Phases 1 and 2 are independent but default to spec order.
4. **Phase 1 persistence** (was open). **Resolution: localStorage (frontend).** New `recentPlayedStore.ts` (Vue reactive + localStorage, reusing the existing safe `loadJSON` pattern from `playerStore.ts:45`). The native SQLite `play_history_v2` table stays separate (statistics, session-end semantics). Move to SQLite only if persistence or query needs grow.
5. **Phase 1 store shape + record hook**. New `recentPlayedStore.ts` module (not extending playerStore, which is already 715 lines). Dedupe key = `FileHash` (matches HistoryView's existing key). `recordRecentPlayed` injected into `PlaybackOrchestrator` deps alongside `uploadPlayHistory`, fired at `playbackOrchestrator.ts:152` after `backend.playUrl()` returns `ok=true` (exactly the spec's "immediately after playback succeeds" boundary). `mergeRemote(remoteTracks)` dedupes by FileHash keeping latest `playedAt`, sorted desc. HistoryView loads local first (immediate render), then remote `/user/history`, then merges.
6. **Phase 2 lyric state**. Composable `useLyricFollow.ts` (not a global store — single consumer is LyricView; not inline refs — needs testability). Exposes `autoFollowing`, `manualScrollUntil`, `activeLineId`, `trackKey`, `onUserScroll()`, `resumeFollow()`, `resetForTrack(key)`. Manual scroll detection via `isProgrammaticScroll` guard flag (set before `scrollIntoView`, checked in scroll handler; flag false = user scroll). Auto-resume after 3s idle with no manual scroll. Visible "return to current" floating button for immediate resume. Track change calls `resetForTrack(trackKey)` (sets `autoFollowing=true`, clears `manualScrollUntil`/`activeLineId`).
7. **Phase 3 diagnostics** (was open). **Resolution: Settings entry.** New "Playback Diagnostics" section in `SettingsView.vue`. New `playbackDiagnostics.ts` module: in-memory ring buffer (200 entries, NOT persisted to localStorage — transient session diagnostics; native crashes aren't captured by frontend diagnostics anyway). Unified event shape `{ts, kind: 'track_switch'|'url_resolve'|'media_event'|'proxy_prep'|'fm_fetch', phase: 'start'|'ok'|'fail'|'noop', detail, trackKey?}`. Recording hooks: `html5Backend.warnMediaEvent`/`error` → `recordEvent(media_event)` (reuses existing `getMediaEventDetails` shape at `html5Backend.ts:180`); orchestrator → `track_switch`/`url_resolve`/`proxy_prep` (via `prepareSourceUrl`); playerStore → `fm_fetch`. Copy button serializes ring buffer via `copyAsText()`.
8. **Phase 4 stall trigger**. Lightweight auto-flag in `playbackDiagnostics.ts`: if `media_event: stalled/waiting` is followed by no `timeupdate`/`play` for 5s, mark a `potential_stall` entry. This is analysis, not recovery (within spec scope — "diagnosed through event collection before adding recovery behavior"). Manual confirmation during 30+ min personal FM listening sessions. Reproduced stalls → separate root-cause task driven by the `systematic-debugging` skill, with diagnostics as evidence.

## Out of Scope for This Spec

- Installer, auto-update, release signing, and GitHub release work.
- Full design system refresh.
- New recommendation algorithms beyond personal FM continuation.
- Native playback backend reactivation.

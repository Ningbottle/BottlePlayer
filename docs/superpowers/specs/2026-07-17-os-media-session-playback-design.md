# OS Media Session & Playback Platform Design

> **Status:** Ready for review (brainstorming direction approved; **reconciled with code baseline**)  
> **Date:** 2026-07-17 (reconciled 2026-07-18)  
> **Approach:** B — deep module `OsMediaSession` as system shell; HTML5 remains primary audio until native is re-enabled  
> **Method:** 对照现状 → 再演进（baseline first, then tracks）  
> **Depends on:** Current mainline tree (native stack present-but-disabled); architecture audit / storage-EQ work as available on the landing branch

## 0. Code baseline (what already exists)

This design was originally drafted as if the dual backend and native audio were greenfield. A code review on 2026-07-17 found otherwise. The plan below is reconciled with reality:

| Area | Spec's original assumption | Actual state in tree |
|------|----------------------------|----------------------|
| `PlayerBackend.kind` | `'html5'` today; widen in T2 | **Already** `'html5' \| 'native'` — [`ui/src/api/playerBackend.ts:18`](../../../ui/src/api/playerBackend.ts) |
| Native backend client | Create in T3 | **Exists**: `NativePlaybackBackend` — [`ui/src/api/nativeBackend.ts`](../../../ui/src/api/nativeBackend.ts) (MFS→MFP fallback, full `playback_*` invoke surface + `playback_event` listen) |
| Native FFI chain | Build later | **Exists end-to-end**: `playback.rs` commands → registered in `lib.rs` → C ABI pointers in `backend_api.rs` → C++ controllers |
| Native engine (MFS) | "Abandoned; must not resurrect" | **Compiled + tested**: `PlaybackControllerMFS.cpp` (447 lines), `PlaybackControllerMFP.cpp`, `EqualizerMFT.cpp` all in `EchoPlayback` (CMake); contract test `EchoPlaybackMfsTest` exists |
| Why native isn't used | — | **Deliberately disabled** at [`ui/src/api/playerStore.ts` `initPlayerBackend`](../../../ui/src/api/playerStore.ts): *"MFS native playback is disabled — topology resolution + deadlock issues. TODO(s4-fix): re-enable native after fixing BuildTopology + deadlock."* Store always constructs `Html5AudioBackend`. |
| OS media session | Absent | **Genuinely absent** — only the `tray-icon` crate is transitively present in `Cargo.lock`; no SMTC / media-key / tray code |

**Consequences for this design:**
- **T1 (OsMediaSession) is the only truly greenfield track** and keeps its original design below.
- **The backend *seam* is done.** What remains on the backend side is **selection/fallback policy** + **removing the hard-disable guard**, not widening an interface or creating adapter files.
- **The native engine is present-but-disabled, not deleted.** The "don't blindly revive MFS" principle still holds, but the real work is *fixing `BuildTopology` + the deadlock in the existing controllers and validating them*, then re-enabling — with replacement (WASAPI) only if that fix proves infeasible.

## 1. Problem

HTML5 + WebView playback works, but **Windows system media surfaces** (taskbar / lock-screen Now Playing, media keys, tray) are weak or missing. A native Media Foundation path (`PlaybackControllerMFS`) exists but is **disabled** due to incomplete topology resolution and a deadlock; it must not be re-enabled as a blind patch.

**Primary goal (user):** improve **playback capability**, starting from **system-level media experience**, without destabilizing the working HTML5 audio path.

## 2. Goals and non-goals

### 2.1 Goals (program)

| Track | Goal |
|-------|------|
| **T1 System session** | New deep module `OsMediaSession`: SMTC Now Playing + media keys + tray; all drive the **same** play/pause/next/prev as in-app UI |
| **T2 Backend selection** | Add runtime **selection + fallback policy** over the existing `PlayerBackend` seam (`html5 \| native`); default HTML5; native opt-in behind a flag; automatic fallback on init failure |
| **T3 Native re-enable** | Fix `BuildTopology` + deadlock in the **existing** `PlaybackControllerMFS`/`MFP`, validate with a spike, and re-enable native output behind the T2 flag — or replace the engine if the fix proves infeasible |

### 2.2 Non-goals

- Re-enabling the current MFS topology in production **without** a green spike proving no deadlock and a complete graph
- A second copy of queue / orchestrator logic for OS buttons
- Shipping macOS/Linux SMTC equivalents in T1 (no-op adapters only)
- One PR that changes Vue + Rust + C++ playback engines at once
- Making native the **default** before selection/fallback contract tests pass
- Rewriting the native adapter files that already exist (`nativeBackend.ts`, `playback.rs`) from scratch

## 3. Principles

1. **Deep module:** `OsMediaSession` hides WinRT / tray / key wiring; callers only set now-playing + status and receive button events.
2. **Single control path:** OS events map only to existing `togglePlay` / `next` / `prev` / `pause` / `resume` (or orchestrator equivalents) — no parallel queue.
3. **HTML5 stays primary:** `<audio>` + `audio_proxy` + WebAudio EQ remain the default and the fallback throughout.
4. **Seam is already there — build on it:** reuse the existing `PlayerBackend` union + `NativePlaybackBackend`; T2 adds only selection/fallback.
5. **Fix, don't blindly revive:** native re-enable (T3) is gated on a spike that resolves the known topology/deadlock issues.
6. **Degrade, don't crash:** SMTC/tray init failure → no-op session; native init failure → fall back to HTML5; playback continues.
7. **Staged PRs:** T1a → T1b → T1c → T2 → T3-spike → T3-impl; each independently testable.

## 4. Target architecture

```
[SMTC / media keys / tray]
          ↕
   OsMediaSession (Rust)          ← T1 NEW: sole OS media owner
          ↕ Tauri commands + events
   ui/src/api/osMediaBridge.ts    ← T1 NEW
          ↕ same app control APIs
   playerStore + PlaybackOrchestrator   (exists)
          ↕
   PlayerBackend  (kind: 'html5' | 'native' — union EXISTS)
     ├─ Html5AudioBackend         (production default, exists)
     └─ NativePlaybackBackend     (EXISTS, currently disabled at initPlayerBackend)
          ↕
   Audio: HTML5 (+ proxy + WebAudio EQ)  or  native (MFS/MFP, disabled)
```

### 4.1 Module map

| Module | Layer | Status | Responsibility |
|--------|--------|--------|----------------|
| `OsMediaSession` | Rust | **T1 new** | Bind/unbind session; metadata; status; enabled controls; emit button events; own SMTC + keys + tray |
| `osMediaBridge.ts` | Vue/TS | **T1 new** | Mirror store → session; session events → app actions; HMR rebind |
| `PlayerBackend` | TS interface | **exists** | Playback control + events; `kind: 'html5' \| 'native'` already declared |
| `Html5AudioBackend` | TS | **exists** | Production default; EQ hooks stay Html5-only options |
| `NativePlaybackBackend` | TS + Rust/C++ | **exists, disabled** | Implements `PlayerBackend`; MFS→MFP fallback via `playback_*` commands; re-enabled in T3 |
| `PlaybackOrchestrator` | TS | **exists** | Owns switchTrack / quality; talks only to `PlayerBackend` |

## 5. Track T1 — OsMediaSession (system shell) — greenfield

### 5.1 Public interface (Rust → Tauri)

Conceptual API (exact names may match crate style):

```text
bind() -> Result<()>
unbind()
set_now_playing(NowPlaying { title, artist, album?, artwork_path_or_url? })
set_playback_status(Playing | Paused | Stopped)
set_enabled_controls(Controls { play_pause, next, prev })
// events (Tauri event channel):
//   os-media://button { "Play" | "Pause" | "PlayPause" | "Next" | "Prev" }
```

**Depth:** COM apartment, SMTC display updater, button handlers, media-key routing, tray icon/menu, artwork download-to-temp (if required by SMTC), all **inside** this module.

### 5.2 Frontend bridge

`ui/src/api/osMediaBridge.ts`:

- On track / play state / queue boundary change → `set_now_playing` / `set_playback_status` / `set_enabled_controls`
- On `os-media://button` → call existing player APIs only (`togglePlay` / `next` / `prev`)
- App start: `bind()` after window ready; app exit / HMR: `unbind()` then optional re-`bind()`
- Must not import WinRT types; only `invoke` / `listen`

### 5.3 MVP surfaces (all in T1, split PRs)

| PR | Surface | Behavior |
|----|---------|----------|
| **T1a** | SMTC / taskbar / lock screen | Title, artist, artwork if available; Play/Pause/Next/Prev buttons |
| **T1b** | Media keys | Same button channel as SMTC (PlayPause/Next/Prev) |
| **T1c** | Tray | Icon; menu: Play/Pause, Next, Prev, Show window, Quit (Quit policy: confirm vs immediate — default immediate quit after stop+unbind) |

> The `tray-icon` crate is already in `Cargo.lock` (transitive via Tauri); T1c wires it explicitly.

### 5.4 T1 data flow

```text
switchTrack success
  → bridge set_now_playing + set_playback_status(Playing)

in-app pause
  → backend.pause → store.isPlaying=false → bridge set_playback_status(Paused)

taskbar Next
  → OsMediaSession event Next → bridge → next() → orchestrator → active backend
```

### 5.5 T1 error / degrade

| Failure | Behavior |
|---------|----------|
| SMTC unavailable | Session no-op; log diagnostic; audio unaffected |
| Artwork fetch fails | Metadata without art |
| Tray create fails | SMTC still works if up |
| Button while no track | no-op; controls should already be disabled via `set_enabled_controls` |

### 5.6 T1 testing

- Rust unit tests against a **fake** `MediaSessionPort` (no WinRT in CI)
- Optional `#[cfg(windows)]` integration tests for real SMTC (local only)
- Vitest: bridge maps store → invoke args; maps events → `next`/`prev`/`togglePlay` mocks
- Full suite green: CTest + vitest + cargo lib

## 6. Track T2 — Backend selection & fallback (seam already exists)

### 6.1 Current state

The interface change the original spec listed as T2 work is **already done**:

```ts
// ui/src/api/playerBackend.ts:18 — already merged
readonly kind: 'html5' | 'native';
```

Both implementations exist (`Html5AudioBackend`, `NativePlaybackBackend`). Html5-only EQ options already live on `Html5AudioBackendOptions`, not on the shared interface. **No interface widening is needed.**

### 6.2 What T2 actually adds

`initPlayerBackend` (in `playerStore.ts`) currently hard-codes HTML5 and early-comments native as disabled. T2 replaces that with an explicit selection policy:

1. Default: `html5`.
2. If setting / feature flag `native_playback` is enabled **and** T3 has landed a working native path: construct `NativePlaybackBackend`, call `initialize()`.
3. On `initialize()` returning `false` (or throw): fall back to `Html5AudioBackend`, record a diagnostic, optional one-shot UI notice.
4. Runtime switch (settings): stop current backend → start other → restore track position best-effort.

Until T3 lands, the flag stays off and step 2 is unreachable — so T2 can merge safely while native is still disabled.

### 6.3 Testing

- Orchestrator tests run against **mock html5** and **mock native** backends.
- Assert selection + fallback logic (flag on + native init fails → ends on html5) without requiring real native audio.
- No requirement that native produces audio in T2 — only that selection + fallback compile and pass mocks.

## 7. Track T3 — Re-enable native audio (fix existing engine)

### 7.1 Reality: the engine exists but is disabled

`PlaybackControllerMFS.cpp` (447 lines), `PlaybackControllerMFP.cpp`, and `EqualizerMFT.cpp` are compiled in `EchoPlayback` and `NativePlaybackBackend` already drives them via `playback_*` commands. Native is off only because `initPlayerBackend` disables it (topology resolution + deadlock). T3 is therefore **debug-and-validate**, not build-from-scratch.

### 7.2 Constraints

- Land only **after** T2 selection/fallback + tests exist.
- **Forbidden:** flip the `native_playback` default on before the spike proves a complete topology with no deadlock.
- Engine decision via **spike** (document result in the plan before full impl):

  | Candidate | Notes |
  |-----------|--------|
  | Fix existing MFS (`BuildTopology` + deadlock) | Preferred: code already exists, tested harness exists (`EchoPlaybackMfsTest`) |
  | Fall back to existing MFP (MFPlay, no EQ) | Already the runtime fallback in `nativeBackend.ts`; lower-risk interim |
  | WASAPI + decoder pipeline | Only if MF proves unfixable |
  | Keep HTML5 only | Acceptable outcome of a failed spike; leave native disabled |

### 7.3 EQ policy (T3)

- WebAudio EQ applies **only** when `kind === 'html5'`.
- Native path uses its own EQ chain (`EqualizerMFT` / `playback_set_eq_bands`), or ships without EQ first — **never both chains active**.
- UI: when native is selected, EQ panel shows "当前为原生输出，EQ 使用 WebAudio 路径时可用" (or equivalent).

### 7.4 FFI

- The `playback_*` C ABI + `backend_api.rs` pointers + `lib.rs` registration already exist — reuse them; do not duplicate.
- Any topology fix stays inside `PlaybackControllerMFS.cpp` / `EqualizerMFT.cpp`; do not reshape the C_API surface without review against current CMake.

## 8. Delivery sequence

| Order | PR | Deliverable | Exit criteria |
|-------|-----|-------------|---------------|
| 1 | T1a | OsMediaSession + SMTC + bridge | Now playing + buttons drive app; CI green |
| 2 | T1b | Media keys | Keys → same events; CI green |
| 3 | T1c | Tray | Menu actions work; degrade if tray fails |
| 4 | T2 | Selection/fallback policy in `initPlayerBackend` + mocks | Tests for both kinds; default still html5; flag off |
| 5 | T3 spike | Fix `BuildTopology`/deadlock in MFS (or MFP interim); spike doc + go/no-go | Written spike result; `EchoPlaybackMfsTest` green under the fix |
| 6 | T3 impl | Re-enable native behind flag; remove disable comment | Feature-flagged; automatic fallback to HTML5 works |

## 9. File touch map (reconciled)

### T1 (create)

- Create: `ui/src-tauri/src/os_media_session.rs` (or `os_media/`)
- Modify: `ui/src-tauri/src/lib.rs` (register commands/events)
- Create: `ui/src/api/osMediaBridge.ts`
- Modify: `ui/src/App.vue` (or player init path) to `bind` bridge
- Tests: `ui/src/api/__tests__/osMediaBridge.test.ts`; Rust unit tests under `os_media_session`

### T2 (modify existing — nothing to create)

- Modify: `ui/src/api/playerStore.ts` — replace the hard-disable in `initPlayerBackend` with selection/fallback
- Reuse: `ui/src/api/playerBackend.ts` (`kind` union already present), `ui/src/api/nativeBackend.ts` (already implements the seam)
- Modify: orchestrator/backend tests for dual mocks

### T3 (fix existing native)

- Modify: `native/playback/PlaybackControllerMFS.cpp` (topology + deadlock), possibly `native/playback/EqualizerMFT.cpp`
- Reuse: `ui/src-tauri/src/playback.rs`, `ui/src-tauri/src/backend_api.rs` (FFI already wired)
- Flip: the `native_playback` flag path in `initPlayerBackend`
- Tests: `native/tests/playback_controller_mfs_test.cpp`

## 10. Risks

| Risk | Mitigation |
|------|------------|
| WinRT/SMTC flaky on some Windows builds | no-op degrade; feature detect |
| Artwork CORS / file path requirements | prefer local cache file path for SMTC |
| Tray + HMR double icons | unbind on cleanup; single owner module |
| Scope creep: touching native audio during T1 | hard gate: no native audio code in T1 PRs |
| Re-enabling MFS before the deadlock is truly fixed | T3 spike gate; keep flag off until `EchoPlaybackMfsTest` green + no deadlock |
| Implementer rebuilds existing native adapter | §0 baseline + §9 "modify/reuse" labels |

## 11. Success metrics

- **T1:** From lock screen / taskbar / media keys / tray, user can control the running app's queue consistently with in-app controls; HTML5 + EQ regression suite still green.
- **T2:** Selection policy and fallback are tested (via mocks) without requiring real native audio; default remains html5.
- **T3:** If shipped, feature-flagged native output works for play/pause/seek/position events; automatic fallback to HTML5 on failure; no deadlock under the MFS contract test.

## 12. Open decisions (resolved in this doc)

| Decision | Choice |
|----------|--------|
| Architecture approach | **B** deep `OsMediaSession` |
| Who produces audio by default | **HTML5** (native is opt-in, T3-gated) |
| Media keys / tray | **In T1** (not deferred forever) |
| Backend seam | **Already exists** — T2 adds selection/fallback only |
| Native engine | **Fix the existing disabled MFS/MFP** (spike-gated); replace only if unfixable — do **not** rebuild from scratch |

## 13. Out of scope forever (unless new brainstorm)

- Multi-tenant EchoContext FFI handles for playback
- Linux MPRIS / macOS Now Playing in the same PRs as T1 Windows
- Replacing WebAudio EQ with native EQ as a requirement for T1

---

## Appendix A — Relation to prior audit / branches

Prior audit work targeted request lifecycle, timeouts/retry ownership, storage binding, EQ leaf extraction, and related cleanups. **This program does not assume MF was deleted from the tree:** on the reconciled baseline, `EchoPlayback` (MFS/MFP/EqualizerMFT), `playback.rs`, and `NativePlaybackBackend` are still present and **only gated off** in `initPlayerBackend`.

Order of work remains: **system media shell first (T1, greenfield)** → **backend selection second (T2, existing seam)** → **native re-enable third (T3, fix disabled MFS/MFP)** — without undoing HTML5 reliability.

## Appendix B — Reconciliation log (2026-07-17 / 2026-07-18)

| Item | Detail |
|------|--------|
| Original draft | Commit `0d801a73` treated T2/T3 as greenfield (“widen `kind`”, “create native adapter”, “spike to pick engine from zero”). |
| Review finding | `kind` union, `NativePlaybackBackend`, full `playback_*` FFI, and compiled+tested MFS/MFP already exist; native is hard-disabled at `initPlayerBackend` for BuildTopology + deadlock. |
| Rewrite | Added §0 baseline; rewrote §2, §4, §6–§9, §12 to *selection wiring* (T2) and *engine fix + re-enable* (T3). **T1 kept** — still greenfield. |
| Superpowers product | This file remains a **design spec** (`docs/superpowers/specs/`), not a TDD task plan (`docs/superpowers/plans/`). Implementation plans are written **after** this spec is approved, track-by-track (recommend **T1a first**). |

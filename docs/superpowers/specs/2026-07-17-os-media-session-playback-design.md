# OS Media Session & Playback Platform Design

> **Status:** Living design — T1a landed on `main` via PR #16 (`e1bae6d4`); baseline re-reconciled **2026-07-18 post-merge**  
> **Date:** 2026-07-17 (reconciled pre-merge; **post-#16 baseline 2026-07-18**)  
> **Approach:** B — deep module `OsMediaSession` as system shell; HTML5 is the **only** audio path on current `main`  
> **Method:** 对照现状 → 再演进（baseline first, then tracks）  
> **Depends on:** `origin/main` at/after merge commit `e1bae6d4` (architecture audit + storage/EQ closeout + T1a)

## 0. Code baseline (post-PR #16 / `origin/main`)

**Authoritative tree:** `main` after merge of #16. Do **not** use pre-merge assumptions (kind union / present-but-disabled MFS).

| Area | Pre-merge false assumption | **Actual state on landed `main`** |
|------|----------------------------|-----------------------------------|
| `PlayerBackend.kind` | `'html5' \| 'native'` | **`'html5'` only** — [`ui/src/api/playerBackend.ts`](../../../ui/src/api/playerBackend.ts) |
| `nativeBackend.ts` | Present | **Deleted** (architecture audit) |
| `playback.rs` / `playback_*` FFI | Present | **Deleted** |
| C++ MFS/MFP / EqualizerMFT | Present-but-disabled | **Deleted** (`native/playback/*` removed from tree and CMake) |
| `initPlayerBackend` | Hard-disable native + TODO | **HTML5-only**: always `new Html5AudioBackend(...)` |
| OS media session | Absent | **T1a present**: [`ui/src-tauri/src/os_media_session.rs`](../../../ui/src-tauri/src/os_media_session.rs) + commands in `lib.rs` |
| Frontend bridge | Absent | **T1a present**: [`ui/src/api/osMediaBridge.ts`](../../../ui/src/api/osMediaBridge.ts); `App.vue` binds only when Tauri shell |
| EQ | — | WebAudio via [`usePlayerEq.ts`](../../../ui/src/api/usePlayerEq.ts) (HTML5 path) |
| Storage | — | Bound SQL + WAL RO concurrency (storage/EQ closeout) |

**Consequences for remaining work:**
- **T1a is done** on `main` (session commands + bridge + tests). Remaining T1 = media keys (T1b) + tray (T1c) + real SMTC WinRT port (today: in-memory session; inject queues buttons).
- **T2 is greenfield again** if native audio returns: widen `kind`, add selection/fallback, reintroduce adapter + FFI — nothing to “un-disable.”
- **T3 cannot “fix existing MFS”** — sources are gone. Any native audio is a **new engine spike** (WASAPI / rebuilt MF / or stay HTML5-only). “Don’t blindly revive broken topology” still applies as a design principle for any new native path.

## 1. Problem

HTML5 + WebView playback works. Windows system media surfaces (taskbar / lock-screen Now Playing, media keys, tray) were missing; **T1a** landed a deep session module + bridge. Real SMTC/media keys/tray OS wiring is incomplete (in-memory port).

Native Media Foundation was **removed** from `main` (incomplete topology / deadlock history). Native is not “disabled in place.”

**Primary goal (user):** improve **playback capability**, starting from **system-level media experience**, without destabilizing HTML5 audio.

## 2. Goals and non-goals

### 2.1 Goals (program)

| Track | Goal | Status on `main` |
|-------|------|------------------|
| **T1a** | OsMediaSession core + bridge (bind/unbind/metadata/status/controls; button inject path) | **Landed** (#16) |
| **T1b** | Media keys → same button channel as session | Not landed |
| **T1c** | Tray menu (play/pause/next/prev/show/quit) | Not landed |
| **T1-SMTC** | WinRT SMTC port behind `MediaSessionPort` (replace in-memory-only) | Not landed |
| **T2** | Dual backend: `kind: 'html5' \| 'native'`, selection + fallback | Not landed (seam not present) |
| **T3** | Native audio engine (spike then implement) behind T2 flag | Not landed; **no in-tree MFS to fix** |

### 2.2 Non-goals

- Blindly re-adding deleted MFS sources without a green spike
- Second copy of queue / orchestrator logic for OS buttons
- macOS/Linux Now Playing in the same PRs as Windows T1b/T1c
- Making native the default without dual-backend tests
- Claiming native is “present-but-disabled” on current `main`

## 3. Principles

1. **Deep module:** `OsMediaSession` hides OS wiring; callers set now-playing/status and receive button events.
2. **Single control path:** OS events map to existing `togglePlay` / `next` / `prev` only.
3. **HTML5 stays primary** until a deliberate native program reintroduces another backend.
4. **Baseline honesty:** design docs track `main` after merges; re-reconcile after architecture-changing landings.
5. **Degrade, don't crash:** session init failure → no-op; playback continues.
6. **Staged PRs:** T1b → T1c → T1-SMTC; optional T2 → T3-spike → T3-impl.

## 4. Target architecture

```
[SMTC / media keys / tray]     ← T1b/T1c/T1-SMTC remaining
          ↕
   OsMediaSession (Rust)       ← T1a LANDED (in-memory port; inject queues)
          ↕ Tauri commands + events
   ui/src/api/osMediaBridge.ts ← T1a LANDED
          ↕ togglePlay / next / prev
   playerStore + PlaybackOrchestrator
          ↕
   PlayerBackend (kind: 'html5' only on main)
     └─ Html5AudioBackend      ← sole audio path
          ↕
   <audio> + proxy + WebAudio EQ (usePlayerEq)
```

Optional future (T2/T3):

```
   PlayerBackend (kind: 'html5' | 'native')
     ├─ Html5AudioBackend
     └─ NativePlaybackBackend  ← reintroduced; engine TBD by spike
```

### 4.1 Module map

| Module | Layer | Status | Responsibility |
|--------|--------|--------|----------------|
| `os_media_session.rs` | Rust | **T1a landed** | Session state; commands; inject queue; `set_app_handle` reserved |
| `osMediaBridge.ts` | Vue/TS | **T1a landed** | Store → session; buttons → player controls; Tauri-only bind |
| `PlayerBackend` | TS | **html5 only** | Playback control + events |
| `Html5AudioBackend` | TS | **production** | Sole backend |
| `usePlayerEq` | TS | **landed** | WebAudio EQ leaf |
| `NativePlaybackBackend` | — | **absent** | Future T2/T3 only |
| `PlaybackOrchestrator` | TS | **exists** | switchTrack / quality |

## 5. Track T1 — OsMediaSession

### 5.1 T1a (landed) — public commands

```text
os_media_bind / os_media_unbind
os_media_set_now_playing / os_media_set_playback_status / os_media_set_enabled_controls
os_media_inject_button   # queues when no live OS emit yet
```

Bridge: `bindOsMediaBridge` / `unbindOsMediaBridge` / `handleOsMediaButton`.  
Tests: `ui/src/api/__tests__/osMediaBridge.test.ts`; Rust `os_media_session::tests`.

### 5.2 Remaining T1 surfaces

| PR | Surface | Behavior |
|----|---------|----------|
| **T1b** | Media keys | Same button channel → bridge handlers |
| **T1c** | Tray | Icon + menu; degrade if tray fails |
| **T1-SMTC** | WinRT SMTC | Port replaces in-memory-only; live emit to `os-media-button` |

### 5.3 T1 data flow (current)

```text
switchTrack success → bridge set_now_playing + set_playback_status(Playing)
in-app pause → store → bridge set_playback_status(Paused)
inject/Next → handleOsMediaButton → next() → orchestrator → Html5
```

### 5.4 T1 error / degrade

| Failure | Behavior |
|---------|----------|
| Non-Tauri / bind fails | Bridge unbound; audio unaffected |
| Artwork missing | Metadata without art |
| SMTC not yet wired | Session still holds state; inject for tests |

## 6. Track T2 — Dual backend (greenfield on current main)

### 6.1 Current state

There is **no** `native` kind and **no** native adapter. T2 is not “selection over an existing seam.”

### 6.2 What T2 must create (if product wants native again)

1. Widen `PlayerBackend.kind` to `'html5' | 'native'`.
2. Add `NativePlaybackBackend` (or equivalent) + Rust/C++ FFI as required by the T3 engine choice.
3. Selection policy in `initPlayerBackend`: default html5; flag `native_playback`; fallback on init failure.
4. Dual-mock orchestrator tests.

## 7. Track T3 — Native audio (new engine, not fix-in-place)

### 7.1 Reality

MFS/MFP/EqualizerMFT sources and tests were **removed** from `main`. There is nothing to re-enable in-tree.

### 7.2 Spike decision (document before impl)

| Candidate | Notes |
|-----------|--------|
| Rebuild MF with complete topology | Only if spike proves no deadlock |
| WASAPI + decoder pipeline | Alternative |
| Stay HTML5-only | Acceptable outcome |

### 7.3 EQ policy

- WebAudio EQ only when `kind === 'html5'`.
- Native EQ (if any) mutually exclusive with WebAudio.

## 8. Delivery sequence (remaining)

| Order | PR | Deliverable | Exit criteria |
|-------|-----|-------------|---------------|
| — | T1a | OsMediaSession + bridge | **Done** (#16) |
| 1 | T1b | Media keys | Keys → bridge; CI green |
| 2 | T1c | Tray | Menu works; degrade ok |
| 3 | T1-SMTC | WinRT port | Lock screen/taskbar Now Playing |
| 4 | T2 | Dual kind + selection (optional) | Mocks + flag default html5 |
| 5 | T3 spike | Engine go/no-go doc | Written result |
| 6 | T3 impl | Native behind flag | Fallback works |

## 9. File touch map (post-#16)

### T1a (done)

- `ui/src-tauri/src/os_media_session.rs`
- `ui/src-tauri/src/lib.rs`
- `ui/src/api/osMediaBridge.ts`
- `ui/src/api/__tests__/osMediaBridge.test.ts`
- `ui/src/App.vue`

### T1b/T1c/T1-SMTC (next)

- Extend `os_media_session.rs` (ports); tray crate wiring; media key registration

### T2/T3 (optional future)

- Create: native adapter + FFI + engine sources as spike dictates
- Modify: `playerBackend.ts`, `playerStore.ts` `initPlayerBackend`

## 10. Risks

| Risk | Mitigation |
|------|------------|
| Spec drift after architecture merges | Re-run §0 baseline against `origin/main` after landings |
| WinRT SMTC flaky | no-op port; feature detect |
| Scope creep re-adding MFS without spike | T3 spike gate |
| Emitter link issues in cargo test | Prefer queue/inject until SMTC port is proven (`tauri::Emitter` previously crashed harness) |

## 11. Success metrics

- **T1a (done):** Commands + bridge tests green; App binds only under Tauri; HTML5 path green.
- **T1 full:** Lock screen / keys / tray control the same queue as in-app.
- **T2/T3:** Only if product still wants native; not required for OS-media success.

## 12. Open decisions (resolved)

| Decision | Choice |
|----------|--------|
| Architecture approach | **B** deep `OsMediaSession` |
| Who produces audio on main | **HTML5 only** |
| Native stack on main after #16 | **Removed**, not disabled-in-place |
| T1a | **Landed** |
| T2/T3 | Optional; T2 reintroduces seam; T3 is new engine spike |

## 13. Out of scope forever (unless new brainstorm)

- Multi-tenant EchoContext FFI handles for playback
- Linux MPRIS / macOS Now Playing in the same PRs as Windows T1
- Replacing WebAudio EQ as a requirement for T1

---

## Appendix A — Relation to audit / #16

Architecture audit removed MF playback and native FFI; storage/EQ closeout landed bound SQL + `usePlayerEq`. PR #16 stacked that work with T1a OsMediaSession and merged to `main`. This document’s **§0 must match that tree**.

## Appendix B — Reconciliation log

| When | Change |
|------|--------|
| 2026-07-17 draft | Assumed T2/T3 greenfield |
| 2026-07-17 review | Pre-audit `main` still had native present-but-disabled → rewrote T2/T3 as “un-disable” |
| 2026-07-18 #16 | Architecture delete of native **landed** with T1a → pre-merge “present-but-disabled” baseline became **false** |
| **2026-07-18 post-merge** | **§0/T2/T3 rewritten again** for `origin/main` @ `e1bae6d4`: kind html5-only; no native files; T1a done; T2/T3 optional greenfield/spike |

## Appendix C — Verification commands (honest baseline)

From a clean checkout of `origin/main`:

```powershell
git rev-parse HEAD   # expect e1bae6d4 or descendant
Test-Path ui/src/api/nativeBackend.ts          # False
Test-Path ui/src-tauri/src/playback.rs         # False
Test-Path native/playback/PlaybackControllerMFS.cpp  # False
Select-String -Path ui/src/api/playerBackend.ts -Pattern "kind:"
# → readonly kind: 'html5';
Test-Path ui/src-tauri/src/os_media_session.rs # True
Test-Path ui/src/api/osMediaBridge.ts          # True
```

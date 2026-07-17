# T1a OsMediaSession + SMTC Bridge Implementation Plan

> **For agentic workers:** Execute task-by-task. Superpowers product: plan under `docs/superpowers/plans/`. Spec: `docs/superpowers/specs/2026-07-17-os-media-session-playback-design.md`.

**Goal:** Ship T1a — deep `OsMediaSession` core + Tauri commands + frontend bridge that mirrors track/play state and routes button events to existing player controls. Real WinRT SMTC is a port; CI uses an in-memory port.

**Architecture:** Port trait owns platform session. Session state machine is pure/testable. Frontend only `invoke`/`listen`.

**Tech Stack:** Rust (Tauri 2), Vue 3 TS, vitest, cargo test.

## Global Constraints

- No native audio / MFS edits in T1a.
- HTML5 playback path unchanged.
- Buttons call existing `togglePlay` / `next` / `prev` only.
- `docs/` may need `git add -f`.

---

### Task 1: Rust session core + unit tests

**Files:**
- Create: `ui/src-tauri/src/os_media_session.rs`
- Modify: `ui/src-tauri/src/lib.rs` (mod + commands)

- [x] Implement MediaSessionPort, InMemoryPort, OsMediaSession, tauri commands
- [x] Unit tests for bind/set/status/button emit path (in-process)
- [x] Register commands in invoke_handler

### Task 2: Frontend bridge + vitest

**Files:**
- Create: `ui/src/api/osMediaBridge.ts`
- Create: `ui/src/api/__tests__/osMediaBridge.test.ts`
- Modify: `ui/src/App.vue` (bind on mount / unbind on unmount)

- [x] Bridge watches store → invoke set_*
- [x] Listens `os-media-button` → player controls
- [x] Tests with mocked invoke/listen

### Task 3: CI typecheck fix (stack)

**Files:**
- Modify: `ui/src/api/playbackQueue.ts` (playTrack return type)
- Modify: `ui/src/api/playerStore.ts` (drop unused flushSaveQueue)

- [x] vue-tsc clean for playerStore leaves

### Task 4: Gates + PR

- [x] vitest osMediaBridge + cargo test os_media_session
- [x] Full gates when possible; open PR to main

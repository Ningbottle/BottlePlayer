# Changelog

All notable changes to BottleMusic will be documented in this file.

## [Unreleased]

### 2026-09-02 — Architecture Remediation: post-audit corrections

A prior draft entry declared "remediation complete, zero structural changes
required post-audit". That claim was falsified by a follow-up audit and is
withdrawn: the tree failed `cargo clippy -- -D warnings` and
`cargo test --lib`, and part of the reported green frontend baseline came
from test files that were not tracked in git.

Corrections now on this branch:

- **Playback:** the volume watcher no longer double-applies EQ volume
  (`html5Backend.setVolume` already forwards to `setEqVolume` when the EQ is
  rerouted); the pre-backend fallback is documented as-is, not "restored".
- **Audio proxy:** LRU eviction gains a deterministic tie-break (monotonic
  touch sequence) for equal `Instant` stamps; stale TTL-era wording removed.
- **FFI dispatch:** admission bounded by a 16-permit semaphore. The cap
  bounds concurrent admission only: timed-out dispatches release their permit
  immediately, but their `spawn_blocking` closures (and backend read guards)
  run to completion and are not cancelled.
- **Tests:** Rust–C++ cross-layer contract tests, frontend shape-contract
  tests, and the Rust integration test targets are now actually run by CI.

Known issues:

- `native/core/compat_routes/YouthVipRoutes.cpp` `HandleYouthDayVip()`
  hardcodes "该端点需要广告 SDK 凭证，纯 HTTP 不可达", but the reference
  implementation (`server/module/youth_day_vip.js`) is plain axios with
  form-urlencoded body and cookies — no ad SDK involved. The attribution is
  wrong and the route deserves a fresh evaluation.

### 2026-02-03T10:00:00
- Create BottleMusic project structure

### 2026-02-10T14:30:00
- Document API authentication and rate limits

### 2026-02-17T09:15:00
- Plan Vue + Tauri + C++ FFI boundary

### 2026-02-24T16:45:00
- Configure VS Code, Rust toolchain, vcpkg

### 2026-03-03T11:20:00
- Setup Vue 3 + Vite frontend scaffold

### 2026-03-10T13:00:00
- CMake + MSVC configuration for EchoCAPI

### 2026-03-17T10:30:00
- WinHTTP wrapper with connection pooling

### 2026-03-24T15:15:00
- Database schema for playlists and cache

### 2026-04-01T09:45:00
- Search, song detail, playlist endpoints

### 2026-04-08T14:00:00
- Thread pool with per-kind deadlines

### 2026-04-15T11:30:00
- Navigation and playlist display

### 2026-04-22T16:20:00
- Search input and navigation arrows

### 2026-05-01T10:00:00
- Transport controls and progress bar

### 2026-05-08T13:45:00
- Basic play/pause/seek functionality

### 2026-05-15T09:30:00
- Featured content and recommended songs

### 2026-05-22T15:00:00
- Frontend resilience for API calls

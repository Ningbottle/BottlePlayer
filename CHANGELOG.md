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

- ~~`native/core/compat_routes/YouthVipRoutes.cpp` `HandleYouthDayVip()`
  hardcodes "该端点需要广告 SDK 凭证，纯 HTTP 不可达"…~~ Resolved 2026-09-02,
  see "VIP claim: re-enable day-VIP routes" below.

### 2026-09-02 — VIP claim: re-enable day-VIP routes against the 2026-08-31 reference contract

- `/youth/day/vip` and `/youth/day/vip/upgrade` now dispatch to
  `UserService::ClaimVip` / `UpgradeVipReward` behind the normal login gate;
  the hardcoded `kugou_vip_legacy_disabled` reject and the `[[deprecated]]`
  markers are removed.
- Both requests were aligned with reference repo v1.6.0
  (`module/youth_day_vip.js`, `util/request.js`, `util/config.json`):
  params stay in the signed URL query with an empty body; the signing
  identity switched from the Concept/lite profile (appid 3116/clientver
  11440/lite salt) to the standard Android profile (appid 1005/clientver
  20489/standard salt); `uuid` pinned to `-`; the unreferenced `plat` param
  removed; `content-type` set to `application/x-www-form-urlencoded`;
  dfid/clienttime/mid/kg-* fingerprint headers added. Upstream numeric
  error codes are passed through to the frontend (old local string codes
  kept as `local_error`). Redacted request/response diagnostics log under
  the `VipClaim` tag.
- Frontend: the account view gains an experimental selector to trigger each
  claim channel individually; failures show the upstream
  status/error_code/error_msg verbatim (see cascade update below).
- Tests: `EchoYouthVipContractTest` pins the new wire contract;
  `EchoRouteContractTest` guards against reintroducing the hardcoded reject.
  Both guards were adversarially verified (implementation broken → red →
  restored → green). Full suite green after forced clean re-links:
  14/14 ctest, 1354 frontend tests, `cargo test --tests`.

Live test later the same day (same account): the re-enabled direct-claim and
upgrade routes plus listen_song were all answered 51002 by upstream despite
the byte-for-byte reference alignment — a business-level wall, not a
request-format problem. The ad `play_report` channel, however, succeeded and
actually granted VIP (3h svip from a single report). Consequently the main
claim button was rebuilt as an ad-first cascade: ad `play_report` loop
(30-second interval, max 8 rounds, breaks on the first rejection), then the
remaining three channels once each until one succeeds; any success syncs the
authoritative VIP detail. Loop bounds are overridable for tests. Frontend
suite green at 1355 tests after the rebuild.

Build note: MSVC `/INCREMENTAL` linking served stale test binaries mid-change
(re-linked exe missed changed static-lib members). Test verification was
redone with forced full re-links; incremental results are not trusted.

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

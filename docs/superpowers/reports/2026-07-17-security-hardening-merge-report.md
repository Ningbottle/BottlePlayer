# Security Hardening + Deep Refactor -- Merge Final Report

**Date:** 2026-07-17
**Merge commit:** `ba4d709b` (--no-ff merge of `codex/dual-interface-deep-refactor` into `main`)
**Pushed to origin/main:** Yes

---

## Summary

The `codex/dual-interface-deep-refactor` branch was merged into `main` via `--no-ff` at commit `ba4d709b` and pushed to `origin/main`. The merge was clean (no conflicts). All verification gates pass on the merged result.

---

## Security Fixes Landed

- **H1 (chokepoint scrub):** Removed inline `Authorization` header construction from 12 routes; all requests now funnel through `createRequestConfig` with token injection.
- **H2 (release.yml bash):** Locked `actions/checkout` to SHA `a5ac7e5` with a trailing `expect` guard; shell injection surface eliminated.
- **M1 (migration closure):** `RustMigration.kt` and `SwiftMigration.kt` deleted; `../rust/` adapter sealed.
- **M2 (credential allowlist):** `.gitignore` hardened to block `*.key`, `*.pem`, `*.p12`, `*.cert`, `credentials*`, `secrets*`, `tokens*`.
- **M3 (buffer zeroing):** `SecureBuffer` (Rust) and `ScopedMemory` (C++) added; secrets zeroed on drop via `Zeroize` / `VirtualFree`.

---

## Cargo Audit Result

- **3 vulnerabilities fixed:** `idna` (IDNA2008), `openssl` (X.509), `tauri` (IPC).
- **18 transitive Tauri dependency warnings** (RustSec unmaintained advisories) deferred to Tauri v2.x upgrade.

---

## Vitest Noise Reduction

- **231 -> 0** Vitest console-noise warnings eliminated by `@vue/compiler-sfc` upgrade and `config.mjs` suppression rules.

---

## Verification Results on Main

| Gate | Result | Detail |
|------|--------|--------|
| Vitest | **PASS** | 63 files, 787 tests |
| vue-tsc --noEmit | **PASS** | No errors |
| vite build | **PASS** | 3496 modules, 485 kB JS + 145 kB CSS |
| cargo test | **PASS** | 31 tests (29 unit + 2 integration) |
| cargo clippy -D warnings | **PASS** | Zero warnings |
| cargo check | **PASS** | Zero warnings |
| CMake configure | **PASS** | bottlemusic-check preset |
| CMake build | **PASS** | 100% targets, all 11 test executables |
| CTest | **PASS** | 11/11 tests passed (40.6s) |

---

## Branch Cleanup

- **4 superseded worktrees + branches removed:** `codex-tsconfig`, `codex-tsconfig-refine`, `codex-verify`, `codex-quickfix`.
- **Stash saved:** `stash@{0}` backs up main's pre-merge candidate changes (`.gitignore`, old `motion.ts`).

---

## Known Follow-ups (per spec section 9)

1. **Real-window visual QA** -- motion sync, queue marker liveness, Aurora particle drift.
2. **Tauri upgrade** -- resolves 18 RustSec transitive warnings.
3. **M4 CSP `connect-src` narrowing** -- requires fixed audio-proxy port.
4. **Store release** -- signing cert, Updater, NSIS installer, VM install.
5. **Third-party service authorization** -- OAuth token exchange.
6. **Minor review notes** -- session_token/set-cookie test coverage, stale jsdom comments.

---

## Commits on This Branch

```
1c7c1900 docs: define dual interface deep refactor plan
8d4d5dcd fix(ui): correct lyric entry and queue follow
f19ef646 fix(ui): calm Aurora particles and follow queue playback
736c1968 docs: add English README.en.md mirror
cb22c89a docs: rewrite README.md for v2 architecture (Chinese)
ba4d709b merge: dual-interface deep refactor + security hardening
```
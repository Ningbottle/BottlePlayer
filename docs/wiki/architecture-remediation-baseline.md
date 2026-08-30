# Architecture Remediation Baseline

Captured on 2026-08-30 at 16:41 (Asia/Shanghai) from commit
`acd55f6d6ab2da15572581470c80362c54542c4a`.

This baseline is the entry gate for the architecture-remediation plan. It records
fresh command results from the prepared workspace; it does not reuse results from
the earlier audit report.

## Environment

- Node.js: `v24.17.0`
- pnpm: `11.8.0`
- CMake: `4.3.1-msvc1`
- cargo: `1.96.0`
- rustc: `1.96.0`
- Native compilation: x64 Visual Studio Developer Command Prompt environment

## Verification results

| Area | Command | Exit | Fresh result |
| --- | --- | ---: | --- |
| Frontend | `pnpm test` | 0 | 86 files passed; 1,077 tests passed; 62.79 s |
| Frontend | `pnpm exec vue-tsc --noEmit` | 0 | Type check passed with no diagnostics |
| Frontend | `pnpm build` | 0 | 3,531 modules transformed; build completed in 11.31 s |
| Native | `cmake --preset bottlemusic-check` | 0 | Configure/generate passed |
| Native | `cmake --build --preset bottlemusic-check` | 0 | Full build completed |
| Native | `ctest --preset bottlemusic-check` | 0 | 14 of 14 tests passed; 28.08 s |
| Rust | `cargo test --lib --no-default-features -- --test-threads=1` | 0 | 36 of 36 tests passed |
| Rust | `cargo check --lib` | 0 | Check passed |
| Rust | `cargo clippy --no-default-features -- -D warnings` | 0 | Clippy passed with warnings denied |
| Tooling | `./ui/scripts/verify-sync-backend.ps1` | 0 | 25 contract assertions passed from a PowerShell 7 parent |
| Tooling | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ui/scripts/verify-sync-backend.ps1` | 0 | 25 contract assertions passed in Windows PowerShell 5.1 |

The production frontend build reports the existing Vite advisory for JavaScript
and CSS chunks larger than 500 kB. It is a performance follow-up, not a build
failure.

An initial native build from a non-developer shell could not locate the MSVC C++
standard-library headers. Re-running the same preset inside the required Visual
Studio developer environment passed completely; this is recorded as an environment
precondition rather than a source failure.

## Recovery and workspace boundary

- Root WIP remains recoverable through branch
  `codex/wip-0830-pre-architecture-remediation` and the retained stash named
  `WIP snapshot before BottleMusic architecture remediation`.
- The `server` submodule has the same branch-and-stash recovery boundary for its
  pre-cleanup untracked lockfile.
- The merged `wiki-audit` linked worktree was removed with `git worktree remove`
  after its filtered file hashes matched its branch commit.
- Root diagnostic artifacts were moved to
  `outputs/workspace-archive-2026-08-30/`; dependency cache and local remediation
  documents remain local and ignored.
- Temporary `.s64` and `.tok` credential/signature intermediates were removed from
  the working tree and remain recoverable from the retained WIP snapshot.

Do not delete either WIP branch or its corresponding stash until the remediation
branch is merged and the repository owner confirms that rollback is no longer
needed.

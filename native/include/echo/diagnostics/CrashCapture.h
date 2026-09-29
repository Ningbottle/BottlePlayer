#pragma once

#include <string>

// Stage 0 of docs/vip-stability-remediation-plan-2026-09-05.md: Release-usable
// crash forensics. The audit (G3) showed both Run-Time Check Failure #2 events
// left no dump and no stack, so no fault could ever be attributed to a build.
// This facility writes, for an unhandled SEH exception:
//   1. a minidump (all thread stacks, thread info, indirectly referenced
//      memory) under the crash directory,
//   2. a plain-text report with the compiled-in build hash, the exception
//      code/address and a symbolized stack walk of the faulting thread.
// It is additive-only: it never changes behavior of healthy code paths, and a
// failed install must never fail initialization (logged and skipped).

#ifndef ECHO_CRASH_BUILD_HASH
#define ECHO_CRASH_BUILD_HASH "unknown"
#endif

namespace echo::diagnostics {

struct CrashCaptureConfig {
  // Directory for dumps + reports. Empty falls back to
  // <temp>/EchoMusicNative/crash so ad-hoc processes (tests, probes) still work.
  std::wstring dump_directory;
  // Identity of the exact source tree; CMake compiles a SHA-256 over the
  // native sources into the library via ECHO_CRASH_BUILD_HASH. Leave empty to
  // use the library's compiled-in identity (the normal case — the hash is
  // only visible where CMake injects the define).
  std::string build_hash;
};

// Create the dump directory and install the unhandled-exception filter.
// Returns false (and installs nothing) when the directory cannot be created.
// Call once per process; later calls overwrite the configuration.
bool InstallCrashCapture(const CrashCaptureConfig& config);

// Resolved dump directory after a successful install; empty when not installed.
std::wstring CrashCaptureDirectory();

// Compiled-in build identity (same value stamped into every report).
std::string CrashCaptureBuildHash();

// Test/probe entry point: runs the same capture core the exception filter
// uses (dump + report) without raising or terminating. Returns the report
// file path as a narrow string, or empty on failure.
std::string WriteCrashReportForTest(const std::string& reason);

}  // namespace echo::diagnostics

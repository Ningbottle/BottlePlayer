#pragma once

// Single source of truth for per-kind request deadlines (ms).
// Outer layers (Rust deadline_for_path, frontend timeout) must stay ≥ these
// values so the C++ scheduler fails first and surfaces a real 504.
//
// Keep ui/src-tauri/src/lib.rs deadline_for_path() in sync with these numbers.

namespace echo::core {

inline constexpr long kDeadlineSongUrlMs = 10000;
inline constexpr long kDeadlineImageMs = 8000;
inline constexpr long kDeadlineLoginPollMs = 6000;
inline constexpr long kDeadlineSearchMs = 12000;
inline constexpr long kDeadlinePlaylistMs = 12000;
inline constexpr long kDeadlineGenericMs = 12000;

// Debug-only diagnostic probe path (/diagnostics/signature-family). NOT a
// production endpoint deadline: it must cover up to four sequential upstream
// probes under one shared wall-clock budget. Release builds never register
// the route; this constant only widens the outer watchdog for that debug path
// and must never be used by any production route.
inline constexpr long kDeadlineSignatureFamilyProbeMs = 60000;

// Shared budget (ms) the C++ probe enforces internally between probe pairs.
// Slightly below the Rust outer deadline so the probe reports NOT_RUN itself
// instead of being killed by the outer watchdog.
inline constexpr long kSignatureFamilyProbeBudgetMs = 45000;

// Frontend outer timeout (ms) — must exceed the largest middle deadline.
inline constexpr long kFrontendTimeoutMs = 14000;

}  // namespace echo::core

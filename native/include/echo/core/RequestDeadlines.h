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

// Frontend outer timeout (ms) — must exceed the largest middle deadline.
inline constexpr long kFrontendTimeoutMs = 14000;

}  // namespace echo::core

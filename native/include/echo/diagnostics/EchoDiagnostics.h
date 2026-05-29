#pragma once

#include <string_view>

namespace echo::diagnostics {

// Lightweight sidecar-visible logging.
// On Windows emits OutputDebugStringA so DebugView can capture it.
// Prefer ECHO_LOG(tag, message) macro for call sites.
void LogDebug(std::string_view tag, std::string_view message);

}  // namespace echo::diagnostics

// Optional macro wrapper for brevity.
#define ECHO_LOG(tag, message) ::echo::diagnostics::LogDebug(tag, message)

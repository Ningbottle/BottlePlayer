#pragma once

#include <string_view>

namespace echo::diagnostics {

// Lightweight sidecar-visible logging.
// On Windows emits OutputDebugStringA so DebugView can capture it.
// Prefer ECHO_LOG(tag, message) macro for call sites.
void LogDebug(std::string_view tag, std::string_view message);

// FFI log callback: level (0=debug, 1=info, 2=warn, 3=error), tag, message, user_data.
using LogCallback = void (*)(int level, const char* tag, const char* msg, void* user_data);

// Install a log callback. Replaces the default OutputDebugString/stderr output.
// Pass nullptr to revert to default.
void SetLogCallback(LogCallback cb, void* user_data);

}  // namespace echo::diagnostics

// Optional macro wrapper for brevity.
#define ECHO_LOG(tag, message) ::echo::diagnostics::LogDebug(tag, message)

#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <iostream>
#endif

#include <sstream>
#include <string>
#include <atomic>

namespace echo::diagnostics {

static std::atomic<LogCallback> g_log_callback{nullptr};
static std::atomic<void*> g_log_user_data{nullptr};

void SetLogCallback(LogCallback cb, void* user_data) {
  g_log_callback.store(cb, std::memory_order_release);
  g_log_user_data.store(user_data, std::memory_order_release);
}

void LogDebug(std::string_view tag, std::string_view message) {
  // P2-O: force redaction at the sink so every log path is scrubbed.
  const std::string redactedMessage = RedactSensitive(message);
  std::ostringstream stream;
  stream << "[" << tag << "] " << redactedMessage;
  const std::string line = stream.str();

  // Forward to FFI callback if installed
  if (LogCallback cb = g_log_callback.load(std::memory_order_acquire)) {
    void* ud = g_log_user_data.load(std::memory_order_acquire);
    std::string tagStr(tag);
    cb(0, tagStr.c_str(), redactedMessage.c_str(), ud);
    return;
  }

#if defined(_WIN32)
  OutputDebugStringA((line + "\n").c_str());
#else
  std::cerr << line << "\n";
#endif
}

}  // namespace echo::diagnostics

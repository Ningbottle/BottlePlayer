#pragma once

#include <optional>
#include <string>

#include "echo/core/Dto.h"
#include "echo/storage/Database.h"

namespace echo::core {

// Lightweight request-scoped context that lazily loads session and device
// from the given Database.  Used by CompatApi routes to avoid repeating
// Repository + Service boilerplate in every handler.
//
// NOT thread-safe; each request should construct its own instance.
class CompatRequestContext {
 public:
  explicit CompatRequestContext(storage::Database& database);

  // Lazy-loads session on first call; caches for the lifetime of this context.
  const std::optional<SessionInfo>& Session();

  // Returns session userId if present and non-empty, otherwise fallback.
  std::string UserIdOr(std::string_view fallback);

  // Returns session token if present, otherwise empty string.
  std::string TokenOrEmpty();

  // True when a non-empty session with both userId and token exists.
  bool HasLogin();

  // Lazy-loads device on first call; caches for the lifetime of this context.
  // Returned reference is valid only while this CompatRequestContext is alive.
  const DeviceInfo& Device();

  // Persist an updated session to the underlying storage.
  // This does not invalidate the cached session_ value.
  void SaveSession(const SessionInfo& info);

  // Persist an updated device to the underlying storage.
  // Also updates the cached device_ so subsequent Device() calls
  // see the latest state.
  void SaveDevice(const DeviceInfo& info);

 private:
  storage::Database& database_;
  std::optional<SessionInfo> session_;
  std::optional<DeviceInfo> device_;
};

}  // namespace echo::core

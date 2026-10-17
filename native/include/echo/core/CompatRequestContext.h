#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "echo/core/Dto.h"
#include "echo/execution/OperationContext.h"

namespace echo::storage { class Database; }

namespace echo::core {

// Lightweight request-scoped context that lazily loads session and device
// from the given Database.  Used by CompatApi routes to avoid repeating
// Repository + Service boilerplate in every handler.
//
// NOT thread-safe; each request should construct its own instance.
//
// Session/device/version capture and optional device initialization are atomic.
// Late network products use typed conditional updates and an explicit operation.
class CompatRequestContext {
 public:
  explicit CompatRequestContext(storage::Database& database,
      execution::OperationContext operation = execution::CurrentOperation());

  // Lazy-loads session on first call; caches for the lifetime of this context.
  const std::optional<SessionInfo>& Session();

  // Returns session userId if present and non-empty, otherwise fallback.
  std::string UserIdOr(std::string_view fallback);

  // Returns session token if present, otherwise empty string.
  std::string TokenOrEmpty();

  // Account generation associated with the lazily loaded session snapshot.
  // Calling this also forces the snapshot to be loaded.
  std::uint64_t SessionGeneration();

  // True when a non-empty session with both userId and token exists.
  bool HasLogin();

  // Lazy-loads device on first call; caches for the lifetime of this context.
  // Returned reference is valid only while this CompatRequestContext is alive.
  const DeviceInfo& Device();

  // Patches adopt the value returned by storage, without replaying a mutator.
  bool PatchProfile(const model::ProfilePatch&);
  bool ReplaceCredentials(const model::CredentialBundle&);
  model::IdentityVersion Version();
  // Conditional registration patch. Never restores a whole request-local device.
  bool SaveDevice(const DeviceInfo& info);
  model::CommitResult RegisterDevice(const std::function<std::string()>& upstream);


 private:
  void Capture();
  storage::Database& database_;
  execution::OperationContext operation_;
  model::IdentityVersion version_;
  std::optional<SessionInfo> session_;
  bool sessionLoaded_ = false;
  std::uint64_t sessionGeneration_ = 0;
  std::optional<DeviceInfo> device_;
};

}  // namespace echo::core

#pragma once

#include <cstdint>
#include <functional>
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
//
// B02 session race protocol: Session() also captures the account generation
// the request loaded at. Full SaveSession stays authoritative (login/logout
// paths bump the generation and must win); a request that only wants to
// apply a late network result — profile fields or refreshed credentials —
// must use the *IfCurrent methods, which commit only while the account
// generation is unchanged, so a logout or a different/repeated login that
// happened during the network wait is never overwritten.
class CompatRequestContext {
 public:
  explicit CompatRequestContext(storage::Database& database);

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

  // Authoritative full write (login/logout/credential-mainline flows) and
  // refresh of the request-local cache. Bumps the account generation.
  void SaveSession(const SessionInfo& info);

  // B02 conditional commit: applies the mutator to the stored session only
  // while the account generation captured by this request is still current.
  // Rejected (returns false, nothing written) after a logout, a login as
  // another account, or a re-login. The request-local cache is updated to
  // the patched view on success so later reads in the same handler stay
  // consistent.
  bool SaveSessionPatchIfCurrent(
      const std::function<void(SessionInfo&)>& mutator);

  // Persist an updated device to the underlying storage.
  // Also updates the cached device_ so subsequent Device() calls
  // see the latest state.
  void SaveDevice(const DeviceInfo& info);

 private:
  storage::Database& database_;
  std::optional<SessionInfo> session_;
  std::uint64_t sessionGeneration_ = 0;
  std::optional<DeviceInfo> device_;
};

}  // namespace echo::core

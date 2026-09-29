#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "echo/core/Dto.h"
#include "echo/storage/Database.h"

namespace echo::storage {

// What a request captures before its network wait, and what every later
// conditional commit is checked against (B02). generation is the account
// generation stored inside the session payload: it is bumped by every
// identity-changing write (login, logout, re-login — including a same-account
// re-login, because the old request's credentials are stale even when the
// userId matches) and left untouched by same-login profile patches and
// credential refreshes. A late response may only mutate the stored session
// while the generation it captured is still current.
struct SessionSnapshot {
  std::optional<echo::core::SessionInfo> session;
  std::uint64_t generation = 0;
};

// A bounded, per-account lease for the lazy /user/detail VIP credential
// refresh. The current credential snapshot travels with the ticket so a
// caller cannot start the refresh using a token cached before the claim.
struct LazyVipRefreshTicket {
  std::uint64_t generation = 0;
  std::string processEpoch;
  std::string claimId;
  echo::core::SessionInfo session;
};

class SessionRepository {
 public:
  explicit SessionRepository(Database& database);

  // Loads the session, performing the one-time encryption migration when it
  // encounters a legacy plaintext payload.
  std::optional<echo::core::SessionInfo> Load();
  // Load() plus the account generation the session was loaded at.
  SessionSnapshot LoadSnapshot();
  // Authoritative full write (login flows). Bumps the account generation and
  // returns it. Unconditional by design: a fresh login must win.
  std::uint64_t Save(const echo::core::SessionInfo& session);
  // Logout. Bumps the account generation so late writers from the ended
  // session are rejected.
  void Clear();
  // B02 conditional commit: applies mutator to the CURRENTLY stored session
  // only if (a) the stored account generation equals expectedGeneration and
  // (b) a non-empty session exists. Read, decision, and write run inside one
  // database actor operation, so no interleaving save can slip between the
  // check and the write. Returns false when rejected (generation moved —
  // logout, another account, or a re-login happened while this request was
  // in flight).
  bool PatchIfGeneration(
      std::uint64_t expectedGeneration,
      const std::function<void(echo::core::SessionInfo&)>& mutator);

  // Atomically claims one lazy VIP refresh while the captured account
  // generation is current and the stored session still has a normal token but
  // no vipToken. The epoch and timestamp are explicit seams for deterministic
  // tests. A new process epoch may reclaim an abandoned in-flight claim; the
  // same epoch may not start a duplicate. No ticket is returned unless the
  // metadata write completed successfully.
  std::optional<LazyVipRefreshTicket> TryClaimLazyVipRefresh(
      std::uint64_t expectedGeneration,
      std::int64_t nowUnixMs,
      const std::string& processEpoch);

  // Finishes exactly the matching in-flight claim and starts a short cooldown
  // even if refresh succeeded without granting vipToken. Safe to call from a
  // noexcept scope guard; malformed/currently superseded claims are a no-op.
  bool FinishLazyVipRefresh(const LazyVipRefreshTicket& ticket,
                            std::int64_t completedUnixMs) noexcept;

  // Stable for the lifetime of this process and suitable for identifying
  // abandoned in-flight metadata written by an earlier process.
  static std::string CurrentProcessEpoch();
  static std::int64_t CurrentUnixTimeMs();

 private:
  Database& database_;
};

}  // namespace echo::storage

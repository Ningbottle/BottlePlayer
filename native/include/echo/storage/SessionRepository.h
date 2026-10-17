#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "echo/model/Identity.h"
#include "echo/execution/OperationContext.h"
#include "echo/ports/AccountStateStore.h"
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
  std::optional<model::SessionInfo> session;
  std::uint64_t generation = 0;
};

// A bounded, per-account lease for the lazy /user/detail VIP credential
// refresh. The current credential snapshot travels with the ticket so a
// caller cannot start the refresh using a token cached before the claim.
struct LazyVipRefreshTicket {
  std::uint64_t generation = 0;
  std::string processEpoch;
  std::string claimId;
  model::SessionInfo session;
};

class SessionRepository : public ports::AccountStateStore {
 public:
  explicit SessionRepository(Database& database);

  // Loads the session, performing the one-time encryption migration when it
  // encounters a legacy plaintext payload.
  std::optional<model::SessionInfo> Load();
  // Load() plus the account generation the session was loaded at.
  SessionSnapshot LoadSnapshot();
  // Startup only, while holding the profile writer lease and before admission.
  void BeginRuntimeEpoch();
  model::IdentitySnapshot CaptureIdentity() override;
  model::IdentitySnapshot CaptureIdentity(
      const std::function<model::DeviceInfo(std::optional<model::DeviceInfo>)>& initialize,
      std::optional<std::uint64_t> expectedAccount = std::nullopt);
  model::CommitResult CommitRegistration(model::IdentityVersion, const model::RegisteredDevicePatch&,
      const execution::OperationContext&) override;
  std::optional<model::SessionInfo> PatchIdentity(model::IdentityVersion,
      const model::ProfilePatch*, const model::CredentialBundle*, const execution::OperationContext&);
  // Authoritative full write (login flows). Bumps the account generation and
  // returns it. Unconditional by design: a fresh login must win.
  std::uint64_t Save(const model::SessionInfo& session);
  // Logout. Bumps the account generation so late writers from the ended
  // session are rejected.
  void Clear();
  void ClearWithDevice();
  // B02 conditional commit: applies mutator to the CURRENTLY stored session
  // only if (a) the stored account generation equals expectedGeneration and
  // (b) a non-empty session exists. Read, decision, and write run inside one
  // database actor operation, so no interleaving save can slip between the
  // check and the write. Returns false when rejected (generation moved —
  // logout, another account, or a re-login happened while this request was
  // in flight).
  bool PatchIfGeneration(
      std::uint64_t expectedGeneration,
      const std::function<void(model::SessionInfo&)>& mutator);

  // Registers an active QR login attempt key, binding it to the session record.
  void SetActiveLoginKey(const std::string& key);
  // Reserve before network work. A late key response cannot replace a newer
  // reservation, nor publish after logout/cancellation.
  std::string BeginLoginAttempt(const std::string& attemptId = "");
  bool PublishLoginKey(const std::string& attemptId, const std::string& key,
                       const std::function<bool()>& canPublish = {},
                       std::optional<model::IdentityVersion> expected = std::nullopt);
  bool IsLoginAttemptCurrent(std::uint64_t generation, const std::string& key);
  void CancelLoginAttempt(const std::string& attemptId);
  // Clears any active QR login attempt key (or matching key).
  void ClearActiveLoginKey(const std::string& key = "");

  // Atomically commits a completed login session if and only if:
  // (a) the stored account generation matches expectedGeneration, and
  // (b) if an active login attempt was registered, it matches key.
  // Returns false if rejected (stale generation or superseded key).
  bool CommitLoginAttempt(
      std::uint64_t expectedGeneration,
      const std::string& key,
      const model::SessionInfo& session,
      const std::optional<model::DeviceInfo>& device = std::nullopt,
      const std::function<bool()>& canCommit = {},
      std::optional<std::uint64_t> expectedDeviceRevision = std::nullopt);
  model::CommitResult ReplaceDevice(model::IdentityVersion, const model::DeviceInfo&,
      const execution::OperationContext&);

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

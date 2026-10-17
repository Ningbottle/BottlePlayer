#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace echo::model {

struct DeviceInfo {
  std::string dfid, mid, uuid, guid, serverDev, mac, appid, clientver;
  bool registered = false;
};
struct SessionInfo {
  std::string token, userId, t1, nickname, pic, vipToken;
  int vipType = 0;
};
struct IdentityVersion {
  std::uint64_t accountGeneration = 0;
  std::uint64_t deviceRevision = 0;
};
struct IdentitySnapshot {
  IdentityVersion version;
  std::optional<SessionInfo> session;
  DeviceInfo device;
};
struct RegisteredDevicePatch { std::string dfid; };
struct ProfilePatch { std::string nickname, pic; };
struct CredentialBundle { std::string token, vipToken, t1; int vipType = 0; };
enum class CommitResult { Applied, StaleAccount, StaleDevice, StaleAttempt, UpstreamFailure, CancelledBeforeCommit, DeadlineBeforeCommit };

}  // namespace echo::model

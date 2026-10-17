#include "echo/core/CompatRequestContext.h"

#include "echo/core/DeviceService.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
#include "echo/application/Registration.h"

namespace echo::core {

CompatRequestContext::CompatRequestContext(storage::Database& database, execution::OperationContext operation)
    : database_(database), operation_(std::move(operation)) {}

void CompatRequestContext::Capture() {
  if (sessionLoaded_) return;
  auto identity = storage::SessionRepository(database_).CaptureIdentity(PrepareDevice);
  session_ = std::move(identity.session);
  device_ = std::move(identity.device);
  version_ = identity.version;
  sessionGeneration_ = version_.accountGeneration;
  sessionLoaded_ = true;
}

const std::optional<SessionInfo>& CompatRequestContext::Session() {
  Capture();
  return session_;
}

model::IdentityVersion CompatRequestContext::Version() { Capture(); return version_; }

std::string CompatRequestContext::UserIdOr(std::string_view fallback) {
  Session();  // ensure loaded
  if (session_.has_value() && !session_->userId.empty()) {
    return session_->userId;
  }
  return std::string(fallback);
}

std::string CompatRequestContext::TokenOrEmpty() {
  Session();  // ensure loaded
  if (session_.has_value()) {
    return session_->token;
  }
  return "";
}

std::uint64_t CompatRequestContext::SessionGeneration() {
  Session();  // ensure the generation belongs to the captured session snapshot
  return sessionGeneration_;
}

const DeviceInfo& CompatRequestContext::Device() {
  Capture();
  return *device_;
}

bool CompatRequestContext::HasLogin() {
  Session();  // ensure loaded
  return session_.has_value() && !session_->userId.empty() && !session_->token.empty();
}

bool CompatRequestContext::PatchProfile(const model::ProfilePatch& patch) {
  Capture();
  auto updated = storage::SessionRepository(database_).PatchIdentity(version_, &patch, nullptr, operation_);
  if (!updated) return false;
  session_ = std::move(updated);
  return true;
}

bool CompatRequestContext::ReplaceCredentials(const model::CredentialBundle& bundle) {
  Capture();
  auto updated = storage::SessionRepository(database_).PatchIdentity(version_, nullptr, &bundle, operation_);
  if (!updated) return false;
  session_ = std::move(updated);
  return true;
}

bool CompatRequestContext::SaveDevice(const DeviceInfo& info) {
  Capture();
  const auto result = storage::SessionRepository(database_).CommitRegistration(version_, {info.dfid}, operation_);
  if (result != model::CommitResult::Applied) return false;
  // The successful commit advances exactly this version. Do not reload and adopt
  // an unrelated newer identity after releasing the transaction.
  ++version_.deviceRevision;
  device_->dfid = info.dfid;
  device_->registered = true;
  return true;
}

model::CommitResult CompatRequestContext::RegisterDevice(const std::function<std::string()>& upstream) {
  Capture();
  storage::SessionRepository store(database_);
  const auto result = application::RegisterDevice(store, {version_, session_, *device_}, operation_, upstream);
  if (result.commit == model::CommitResult::Applied) {
    ++version_.deviceRevision;
    device_ = result.device;
  }
  return result.commit;
}

}  // namespace echo::core

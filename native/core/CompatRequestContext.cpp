#include "echo/core/CompatRequestContext.h"

#include "echo/core/DeviceService.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

namespace echo::core {

CompatRequestContext::CompatRequestContext(storage::Database& database)
    : database_(database) {}

const std::optional<SessionInfo>& CompatRequestContext::Session() {
  if (!session_.has_value()) {
    storage::SessionRepository repo(database_);
    auto snapshot = repo.LoadSnapshot();
    session_ = std::move(snapshot.session);
    sessionGeneration_ = snapshot.generation;
  }
  return session_;
}

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
  if (!device_.has_value()) {
    storage::DeviceRepository repo(database_);
    DeviceService service(repo);
    device_ = service.EnsureDeviceReady();
  }
  return *device_;
}

bool CompatRequestContext::HasLogin() {
  Session();  // ensure loaded
  return session_.has_value() && !session_->userId.empty() && !session_->token.empty();
}

void CompatRequestContext::SaveSession(const SessionInfo& info) {
  storage::SessionRepository repo(database_);
  // Authoritative full save: bumps the account generation, and this request
  // adopts the new generation so its own later conditional patches apply.
  sessionGeneration_ = repo.Save(info);
  session_ = info;
}

bool CompatRequestContext::SaveSessionPatchIfCurrent(
    const std::function<void(SessionInfo&)>& mutator) {
  Session();  // ensure loaded and generation captured
  if (!session_.has_value()) return false;
  storage::SessionRepository repo(database_);
  const bool applied = repo.PatchIfGeneration(sessionGeneration_, mutator);
  if (applied) {
    // Mirror the patch into the request-local cache so the handler's later
    // reads (response fallbacks, follow-up comparisons) see what was stored.
    mutator(*session_);
  }
  return applied;
}

void CompatRequestContext::SaveDevice(const DeviceInfo& info) {
  storage::DeviceRepository repo(database_);
  repo.Save(info);
  device_ = info;
}

}  // namespace echo::core

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
    session_ = repo.Load();
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
  repo.Save(info);
}

void CompatRequestContext::SaveDevice(const DeviceInfo& info) {
  storage::DeviceRepository repo(database_);
  repo.Save(info);
  device_ = info;
}

}  // namespace echo::core

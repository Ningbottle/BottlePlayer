#include "echo/storage/SessionRepository.h"

#include "echo/core/JsonHelpers.h"

namespace echo::storage {

SessionRepository::SessionRepository(Database& database) : database_(database) {}

std::optional<echo::core::SessionInfo> SessionRepository::Load() {
  auto payload = database_.GetJson("session.info");
  if (!payload) return std::nullopt;
  return echo::core::SessionInfoFromJson(*payload);
}

void SessionRepository::Save(const echo::core::SessionInfo& session) {
  database_.SetJson("session.info", echo::core::ToJson(session));
}

void SessionRepository::Clear() {
  database_.SetJson("session.info", nlohmann::json::object());
}

}  // namespace echo::storage


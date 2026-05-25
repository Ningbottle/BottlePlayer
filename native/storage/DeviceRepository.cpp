#include "echo/storage/DeviceRepository.h"

#include "echo/core/JsonHelpers.h"

namespace echo::storage {

DeviceRepository::DeviceRepository(Database& database) : database_(database) {}

std::optional<echo::core::DeviceInfo> DeviceRepository::Load() {
  auto payload = database_.GetJson("device.info");
  if (!payload) return std::nullopt;
  return echo::core::DeviceInfoFromJson(*payload);
}

void DeviceRepository::Save(const echo::core::DeviceInfo& device) {
  database_.SetJson("device.info", echo::core::ToJson(device));
}

void DeviceRepository::Clear() {
  database_.SetJson("device.info", nlohmann::json::object());
}

}  // namespace echo::storage


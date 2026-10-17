#include "echo/storage/DeviceRepository.h"

#include "echo/storage/IdentityCodec.h"
#include "echo/diagnostics/EchoDiagnostics.h"

namespace echo::storage {
DeviceRepository::DeviceRepository(Database& database) : database_(database) {}

std::optional<model::DeviceInfo> DeviceRepository::Load() {
  auto payload = database_.GetJson("device.info");
  if (!payload) return std::nullopt;
  if (!IsWellFormedDeviceRecord(*payload)) {
    ECHO_LOG("DeviceRepo", "malformed device.info ignored -> treated as missing");
    return std::nullopt;
  }
  return DecodeDevice(*payload);
}

model::DeviceInfo DeviceRepository::Ensure(
    const std::function<model::DeviceInfo(std::optional<model::DeviceInfo>)>& normalize) {
  model::DeviceInfo result;
  database_.UpdateJson("device.info", [&](auto current) -> std::optional<nlohmann::json> {
    auto existing = current && IsWellFormedDeviceRecord(*current) ?
        std::optional{DecodeDevice(*current)} : std::nullopt;
    result = normalize(existing);
    auto payload = EncodeDevice(result);
    if (existing && EncodeDevice(*existing) == payload) return std::nullopt;
    return StampDevice(std::move(payload), current);
  });
  return result;
}

void DeviceRepository::Save(const model::DeviceInfo& device) {
  database_.UpdateJson("device.info", [&](auto current) { return StampDevice(EncodeDevice(device), current); });
}

void DeviceRepository::Clear() {
  database_.UpdateJson("device.info", [](auto current) { return StampDevice(nlohmann::json::object(), current); });
}

}  // namespace echo::storage

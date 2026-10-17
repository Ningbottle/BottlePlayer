#pragma once

#include "echo/model/Identity.h"
#include <nlohmann/json.hpp>

namespace echo::storage {
nlohmann::json EncodeDevice(const model::DeviceInfo&);
nlohmann::json EncodeSession(const model::SessionInfo&);
model::DeviceInfo DecodeDevice(const nlohmann::json&);
model::SessionInfo DecodeSession(const nlohmann::json&);
bool IsWellFormedDeviceRecord(const nlohmann::json&);
std::uint64_t DeviceRevision(const std::optional<nlohmann::json>&);
nlohmann::json StampDevice(nlohmann::json, const std::optional<nlohmann::json>&);
}  // namespace echo::storage

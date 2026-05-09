#pragma once

#include <nlohmann/json.hpp>

#include "echo/core/Dto.h"

namespace echo::core {

nlohmann::json ToJson(const DeviceInfo& device);
nlohmann::json ToJson(const SessionInfo& session);
DeviceInfo DeviceInfoFromJson(const nlohmann::json& value);
SessionInfo SessionInfoFromJson(const nlohmann::json& value);

}  // namespace echo::core


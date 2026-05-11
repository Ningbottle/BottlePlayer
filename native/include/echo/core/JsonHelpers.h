#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "echo/core/Dto.h"

namespace echo::core {

nlohmann::json ToJson(const DeviceInfo& device);
nlohmann::json ToJson(const SessionInfo& session);
DeviceInfo DeviceInfoFromJson(const nlohmann::json& value);
SessionInfo SessionInfoFromJson(const nlohmann::json& value);
bool ContractJsonMatches(
    const nlohmann::json& expected,
    const nlohmann::json& actual,
    const std::vector<std::string>& ignoredPaths,
    std::vector<std::string>* mismatches = nullptr);

}  // namespace echo::core

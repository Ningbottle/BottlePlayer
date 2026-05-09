#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/storage/Database.h"

namespace echo::storage {

class ApiCache {
 public:
  explicit ApiCache(Database& database);

  std::optional<nlohmann::json> Get(const std::string& cacheKey);
  void Put(const std::string& cacheKey, const nlohmann::json& payload, std::chrono::seconds ttl);
  void PruneExpired();

 private:
  Database& database_;
};

}  // namespace echo::storage


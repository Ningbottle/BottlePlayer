#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/Authorization.h"
#include "echo/storage/Database.h"

namespace echo::core {

struct CompatResponse {
  int httpStatus = 200;
  std::string contentType = "application/json; charset=utf-8";
  nlohmann::json body;
};

class CompatApi {
 public:
  explicit CompatApi(storage::Database& database);

  CompatResponse Handle(
      const std::string& method,
      const std::string& path,
      const QueryMap& query,
      const HeaderMap& headers);

 private:
  storage::Database& database_;

  CompatResponse HandleKnownRoute(
      const std::string& method,
      const std::string& path,
      const QueryMap& query,
      const HeaderMap& headers);
};

bool IsKnownCompatRoute(const std::string& path);
nlohmann::json NativeNotImplementedPayload(const std::string& path);

}  // namespace echo::core


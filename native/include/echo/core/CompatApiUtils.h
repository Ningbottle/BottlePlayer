#pragma once

#include <chrono>
#include <string>

#include "echo/core/CompatApi.h"
#include "echo/diagnostics/EchoDiagnostics.h"

namespace echo::core {

using namespace std::chrono;

// Time helpers
inline std::int64_t UnixSeconds() {
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

inline std::int64_t UnixMilliseconds() {
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// JSON response helper
inline CompatResponse JsonResponse(nlohmann::json body, int httpStatus = 200) {
  ECHO_LOG("CompatApi", std::string("JsonResponse entry, httpStatus=") + std::to_string(httpStatus));
  CompatResponse resp{httpStatus, "application/json; charset=utf-8", std::move(body)};
  ECHO_LOG("CompatApi", "JsonResponse constructed, returning");
  return resp;
}

inline nlohmann::json EmptyPagedData() {
  return {
      {"status", 1},
      {"data",
       {
           {"lists", nlohmann::json::array()},
           {"list", nlohmann::json::array()},
           {"total", 0},
       }},
  };
}

// Query helpers
inline std::string QueryValue(const QueryMap& query, const std::string& key, std::string fallback = "") {
  const auto it = query.find(key);
  return it == query.end() ? std::move(fallback) : it->second;
}

inline int QueryInt(const QueryMap& query, const std::string& key, int fallback) {
  const auto value = QueryValue(query, key);
  if (value.empty()) return fallback;
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

inline bool IsKuGouErrorCode(const nlohmann::json& body, int code) {
  if (!body.is_object() || !body.contains("error_code")) return false;
  const auto& value = body.at("error_code");
  if (value.is_number_integer()) return value.get<int>() == code;
  if (value.is_string()) {
    try {
      return std::stoi(value.get<std::string>()) == code;
    } catch (...) {
      return false;
    }
  }
  return false;
}

inline void StripSessionCredentials(nlohmann::json& value) {
  if (value.is_object()) {
    value.erase("token");
    value.erase("t1");
    for (auto& [_, child] : value.items()) {
      StripSessionCredentials(child);
    }
    return;
  }
  if (value.is_array()) {
    for (auto& child : value) {
      StripSessionCredentials(child);
    }
  }
}

}  // namespace echo::core

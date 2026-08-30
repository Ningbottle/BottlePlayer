#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "echo/core/CompatApi.h"
#include "echo/core/Crypto.h"
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

// VIP detail: upstream failure must stay non-authoritative. Never invent is_vip=0.
inline nlohmann::json NormalizeUserVipDetailResponse(nlohmann::json vip) {
  if (vip.is_object() && vip.value("status", 0) == 1 && vip.contains("data") &&
      vip["data"].is_object()) {
    vip["authoritative"] = true;
    return vip;
  }

  nlohmann::json out = {
      {"status", 0},
      {"authoritative", false},
      {"data", nullptr},
  };

  if (vip.is_object() && vip.contains("error_code")) {
    out["error_code"] = vip["error_code"];
  } else if (vip.is_object() && vip.contains("errcode")) {
    out["error_code"] = vip["errcode"];
    out["errcode"] = vip["errcode"];
  } else {
    out["error_code"] = "native_vip_detail_failed";
  }

  std::string message;
  if (vip.is_object()) {
    for (const char* key : {"error", "error_msg", "msg", "message"}) {
      if (vip.contains(key) && vip[key].is_string() && !vip[key].get<std::string>().empty()) {
        message = vip[key].get<std::string>();
        break;
      }
    }
    if (vip.contains("error_msg")) {
      out["error_msg"] = vip["error_msg"];
    }
    if (vip.contains("errcode") && !out.contains("errcode")) {
      out["errcode"] = vip["errcode"];
    }
  }
  if (message.empty()) {
    message = "vip detail failed";
  }
  out["error"] = message;
  if (!out.contains("error_msg")) {
    out["error_msg"] = message;
  }
  return out;
}

inline std::optional<int> ReadKuGouErrorCodeValue(const nlohmann::json& value) {
  if (value.is_number_integer()) return value.get<int>();
  if (value.is_number_unsigned()) return static_cast<int>(value.get<unsigned int>());
  if (value.is_string()) {
    const auto& text = value.get<std::string>();
    if (text.empty()) return std::nullopt;
    try {
      std::size_t idx = 0;
      const int parsed = std::stoi(text, &idx);
      if (idx == text.size()) return parsed;
    } catch (...) {
    }
  }
  return std::nullopt;
}

// Top-level only. Prefer error_code when both exist and conflict.
inline std::optional<int> ReadKuGouErrorCode(const nlohmann::json& body) {
  if (!body.is_object()) return std::nullopt;
  std::optional<int> errorCode;
  std::optional<int> errcode;
  if (body.contains("error_code")) {
    errorCode = ReadKuGouErrorCodeValue(body.at("error_code"));
  }
  if (body.contains("errcode")) {
    errcode = ReadKuGouErrorCodeValue(body.at("errcode"));
  }
  if (errorCode && errcode && *errorCode != *errcode) {
    ECHO_LOG("CompatApi", "error_code/errcode conflict, preferring error_code");
    return errorCode;
  }
  if (errorCode) return errorCode;
  return errcode;
}

inline bool IsKuGouErrorCode(const nlohmann::json& body, int code) {
  const auto actual = ReadKuGouErrorCode(body);
  return actual.has_value() && *actual == code;
}

inline std::string DescribeDeviceIdentity(const DeviceInfo& device) {
  const bool hasDfid = !device.dfid.empty() && device.dfid != "-";
  const bool androidMid =
      (device.mid.size() == 38 || device.mid.size() == 39) &&
      std::all_of(device.mid.begin(), device.mid.end(),
                  [](unsigned char c) { return std::isdigit(c); });
  const auto dfidFingerprint = hasDfid
                                   ? CalculateMd5(device.dfid).substr(0, 8)
                                   : std::string{"-"};

  std::ostringstream out;
  out << "registered=" << (device.registered ? "Y" : "N")
      << " dfid_fp=" << dfidFingerprint
      << " dfid_len=" << (hasDfid ? device.dfid.size() : 0)
      << " mid_kind=" << (androidMid ? "android" : (device.mid.empty() ? "empty" : "other"))
      << " mid_len=" << device.mid.size()
      << " uuid_len=" << device.uuid.size()
      << " guid_present=" << (device.guid.empty() ? "N" : "Y")
      << " appid=" << device.appid
      << " clientver=" << device.clientver;
  return out.str();
}

inline bool IsCredentialKey(const std::string& key) {
  static const std::unordered_set<std::string> kCredentialKeys = {
      "token", "t1", "access_token", "auth_token", "session_token",
      "secret", "cookie", "set-cookie", "signature"};
  std::string lowered;
  lowered.reserve(key.size());
  for (unsigned char c : key) lowered.push_back(static_cast<char>(std::tolower(c)));
  return kCredentialKeys.count(lowered) > 0;
}

inline void StripSessionCredentials(nlohmann::json& value) {
  if (value.is_object()) {
    std::vector<std::string> toErase;
    for (auto& [key, child] : value.items()) {
      if (IsCredentialKey(key)) toErase.push_back(key);
    }
    for (const auto& key : toErase) value.erase(key);
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

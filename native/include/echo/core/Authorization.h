#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace echo::core {

using HeaderMap = std::unordered_map<std::string, std::string>;
using QueryMap = std::unordered_map<std::string, std::string>;

struct AuthContext {
  std::optional<std::string> token;
  std::optional<std::string> userId;
  std::optional<std::string> t1;
  std::optional<std::string> dfid;
  std::optional<std::string> mid;
  std::optional<std::string> uuid;
  std::optional<std::string> guid;
  std::optional<std::string> serverDev;
  std::optional<std::string> mac;
};

AuthContext ParseAuthorizationHeader(const std::string& value);
AuthContext ParseAuthorizationHeaders(const HeaderMap& headers);

}  // namespace echo::core


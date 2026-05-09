#include "echo/core/Authorization.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace echo::core {
namespace {

std::string Trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
                return !is_space(static_cast<unsigned char>(ch));
              }).base(),
              value.end());
  return value;
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

void Assign(AuthContext& auth, const std::string& key, std::string value) {
  const auto normalized = Lower(key);
  if (normalized == "token") auth.token = std::move(value);
  else if (normalized == "userid") auth.userId = std::move(value);
  else if (normalized == "t1") auth.t1 = std::move(value);
  else if (normalized == "dfid") auth.dfid = std::move(value);
  else if (normalized == "kugou_api_mid") auth.mid = std::move(value);
  else if (normalized == "uuid") auth.uuid = std::move(value);
  else if (normalized == "kugou_api_guid") auth.guid = std::move(value);
  else if (normalized == "kugou_api_dev") auth.serverDev = std::move(value);
  else if (normalized == "kugou_api_mac") auth.mac = std::move(value);
}

}  // namespace

AuthContext ParseAuthorizationHeader(const std::string& value) {
  AuthContext auth;
  std::stringstream stream(value);
  std::string part;

  while (std::getline(stream, part, ';')) {
    const auto separator = part.find('=');
    if (separator == std::string::npos) continue;

    const auto key = Trim(part.substr(0, separator));
    auto fieldValue = Trim(part.substr(separator + 1));
    if (!key.empty() && !fieldValue.empty()) {
      Assign(auth, key, std::move(fieldValue));
    }
  }

  return auth;
}

AuthContext ParseAuthorizationHeaders(const HeaderMap& headers) {
  for (const auto& [key, value] : headers) {
    if (Lower(key) == "authorization") {
      return ParseAuthorizationHeader(value);
    }
  }
  return {};
}

}  // namespace echo::core


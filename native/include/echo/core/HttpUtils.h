#pragma once

#include <string>
#include <string_view>

#include "echo/core/Authorization.h"

namespace echo::core {

std::string UrlDecode(std::string_view value);
std::string Trim(std::string value);
std::string ToLowerAscii(std::string value);

bool ParseHttpRequest(
    const std::string& raw,
    std::string& method,
    std::string& path,
    QueryMap& query,
    HeaderMap& headers);

}  // namespace echo::core

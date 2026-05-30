#pragma once

#include <string>
#include <string_view>

namespace echo::core {

// RFC 3986 percent encoding (uppercase hex).
// Safe chars: A-Z a-z 0-9 - _ . ~
std::string UrlEncode(std::string_view value);

}  // namespace echo::core

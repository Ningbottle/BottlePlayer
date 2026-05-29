#pragma once

#include <string>
#include <string_view>

namespace echo::diagnostics {

std::string RedactSensitive(std::string_view text);
std::string TruncateForLog(std::string_view text, std::size_t maxBytes = 512);
std::string MaskMiddle(std::string_view value, std::size_t prefix = 3,
                       std::size_t suffix = 3);

}  // namespace echo::diagnostics

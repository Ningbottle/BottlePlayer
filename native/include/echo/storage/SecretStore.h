#pragma once

#include <optional>
#include <string>

namespace echo::storage {

std::string ProtectForCurrentUser(const std::string& plaintext);
std::optional<std::string> UnprotectForCurrentUser(const std::string& encoded);

}  // namespace echo::storage

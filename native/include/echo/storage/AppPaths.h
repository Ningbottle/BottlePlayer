#pragma once

#include <filesystem>

namespace echo::storage {

std::filesystem::path GetAppDataDirectory();
std::filesystem::path GetDefaultDatabasePath();

}  // namespace echo::storage


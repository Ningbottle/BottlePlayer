#include "echo/storage/AppPaths.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace echo::storage {
namespace {

std::filesystem::path EnvironmentPath(const char* name) {
  const char* value = std::getenv(name);
  return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path EnsureDirectory(std::filesystem::path directory) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (!error) return directory;
  directory = std::filesystem::temp_directory_path(error) / "EchoMusicNative";
  if (!error) std::filesystem::create_directories(directory, error);
  if (error) throw std::filesystem::filesystem_error(
      "create EchoMusicNative data directory", directory, error);
  return directory;
}

}  // namespace

std::filesystem::path GetAppDataDirectory() {
  const auto overrideDirectory = EnvironmentPath("ECHO_NATIVE_DATA_DIR");
  if (!overrideDirectory.empty()) {
    return EnsureDirectory(overrideDirectory);
  }

  auto base = EnvironmentPath("XDG_DATA_HOME");
  if (base.empty()) {
    const auto home = EnvironmentPath("HOME");
    if (!home.empty()) {
      base = home / ".local" / "share";
    }
  }
  if (base.empty()) {
    base = std::filesystem::temp_directory_path();
  }
  return EnsureDirectory(base / "EchoMusicNative");
}

std::filesystem::path GetDefaultDatabasePath() {
  return GetAppDataDirectory() / "echomusic-native.db";
}

}  // namespace echo::storage

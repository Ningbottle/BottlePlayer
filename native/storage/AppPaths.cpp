#include "echo/storage/AppPaths.h"

#include <windows.h>

namespace echo::storage {
namespace {

std::filesystem::path EnvPath(const wchar_t* name) {
  wchar_t buffer[MAX_PATH]{};
  const auto length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return {};
  return std::filesystem::path(buffer);
}

}  // namespace

std::filesystem::path GetAppDataDirectory() {
  auto overrideDir = EnvPath(L"ECHO_NATIVE_DATA_DIR");
  if (!overrideDir.empty()) {
    std::filesystem::create_directories(overrideDir);
    return overrideDir;
  }

  auto base = EnvPath(L"LOCALAPPDATA");
  if (base.empty()) base = std::filesystem::temp_directory_path();
  auto dir = base / L"EchoMusicNative";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) {
    dir = std::filesystem::temp_directory_path() / L"EchoMusicNative";
    std::filesystem::create_directories(dir);
  }
  return dir;
}

std::filesystem::path GetDefaultDatabasePath() {
  return GetAppDataDirectory() / L"echomusic-native.db";
}

}  // namespace echo::storage

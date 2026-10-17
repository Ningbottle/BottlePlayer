#include "echo/storage/ProfileWriterLease.h"
#include <windows.h>
#include <stdexcept>

namespace echo::storage {
ProfileWriterLease::ProfileWriterLease(const std::filesystem::path& databasePath) {
  const auto directory = std::filesystem::absolute(databasePath).parent_path();
  std::filesystem::create_directories(directory);
  const auto lockPath = directory / L".bottlemusic.writer.lock";
  auto handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
  if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("profile_writer_unavailable");
  handle_ = handle;
}
ProfileWriterLease::~ProfileWriterLease() {
  if (handle_) CloseHandle(handle_);
}
}

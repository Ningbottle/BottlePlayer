#include "echo/storage/ProfileWriterLease.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace echo::storage {

ProfileWriterLease::ProfileWriterLease(const std::filesystem::path& databasePath) {
  const auto directory = std::filesystem::absolute(databasePath).parent_path();
  std::filesystem::create_directories(directory);
  const auto lockPath = directory / ".bottlemusic.writer.lock";
  const int fd = open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
    if (fd >= 0) close(fd);
    throw std::runtime_error("profile_writer_unavailable");
  }
  handle_ = reinterpret_cast<void*>(static_cast<intptr_t>(fd) + 1);
}

ProfileWriterLease::~ProfileWriterLease() {
  if (!handle_) return;
  const auto fd = static_cast<int>(reinterpret_cast<intptr_t>(handle_) - 1);
  flock(fd, LOCK_UN);
  close(fd);
}

}  // namespace echo::storage

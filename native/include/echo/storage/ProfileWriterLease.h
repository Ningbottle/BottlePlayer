#pragma once
#include <filesystem>

namespace echo::storage {
// One cooperating Native Runtime per profile. Other Database connections in
// this process still use SQLite transactions. Legacy binaries must be stopped
// before upgrading: they do not participate in this lease protocol.
class ProfileWriterLease {
 public:
  explicit ProfileWriterLease(const std::filesystem::path& databasePath);
  ~ProfileWriterLease();
  ProfileWriterLease(const ProfileWriterLease&) = delete;
  ProfileWriterLease& operator=(const ProfileWriterLease&) = delete;
 private:
  void* handle_ = nullptr;
};
}

#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"

#include <chrono>
#include <filesystem>
#include <iostream>

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed: " #condition << " at line " << __LINE__     \
                << std::endl;                                                   \
      return 1;                                                                 \
    }                                                                           \
  } while (false)

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dbPath = std::filesystem::temp_directory_path() /
                      ("bottlemusic-device-identity-" + std::to_string(nonce) + ".db");

  {
    echo::storage::Database database;
    database.Open(dbPath.string());
    database.Initialize();
    echo::storage::DeviceRepository devices(database);
    devices.Save(echo::core::DeviceInfo{
        .dfid = "legacy-issued-dfid-12345",
        .mid = "abcdef0123456789abcdef0123456789abcdef0",
        .uuid = "stable-legacy-uuid",
        .guid = "",
        .serverDev = "",
        .mac = "02:00:00:00:00:00",
        .appid = "3116",
        .clientver = "11440",
        .registered = true,
    });

    echo::core::DeviceService service(devices);
    const auto migrated = service.EnsureDeviceReady();
    CHECK(migrated.guid == "stable-legacy-uuid");
    CHECK(echo::core::ResolveAndroidMid(migrated) ==
          echo::core::CalculateAndroidMid("stable-legacy-uuid"));
    CHECK(migrated.registered == false);

    const auto persisted = devices.Load();
    CHECK(persisted.has_value());
    CHECK(persisted->guid == "stable-legacy-uuid");
    CHECK(persisted->registered == false);
  }

  std::error_code ignored;
  std::filesystem::remove(dbPath, ignored);
  std::filesystem::remove(dbPath.string() + "-wal", ignored);
  std::filesystem::remove(dbPath.string() + "-shm", ignored);
  std::cout << "[DeviceIdentityContract] legacy identity migrated" << std::endl;
  return 0;
}

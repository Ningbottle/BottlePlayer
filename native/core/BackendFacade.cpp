#include "echo/core/BackendFacade.h"

#include <filesystem>
#include <future>

#include "echo/core/DeviceService.h"
#include "echo/core/LyricService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongUrlService.h"
#include "echo/storage/AppPaths.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SettingsRepository.h"

namespace echo::core {
namespace {

std::future<nlohmann::json> NativeAsyncNotImplemented(std::string operation) {
  return std::async(std::launch::deferred, [operation = std::move(operation)] {
    return nlohmann::json{
        {"status", 0},
        {"error_code", "native_not_implemented"},
        {"error", operation + " has not been ported yet"}};
  });
}

AppSettings ToCoreSettings(const storage::AppSettings& settings) {
  return AppSettings{
      settings.volume,
      settings.startupPage,
      settings.imageMemoryCacheMb,
  };
}

storage::AppSettings ToStorageSettings(const AppSettings& settings) {
  return storage::AppSettings{
      settings.volume,
      settings.startupPage,
      settings.imageMemoryCacheMb,
  };
}

class BackendFacade final : public IBackendFacade {
 public:
  explicit BackendFacade(std::filesystem::path databasePath)
      : databasePath_(std::move(databasePath)) {
    storage::Database database;
    database.Open(databasePath_);
    database.Initialize();
  }

  std::future<DeviceInfo> EnsureDeviceReady() override {
    return std::async(std::launch::async, [databasePath = databasePath_] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();
      storage::DeviceRepository repository(database);
      DeviceService service(repository);
      return service.EnsureDeviceReady();
    });
  }

  std::future<nlohmann::json> BeginQrLogin() override {
    return NativeAsyncNotImplemented("BeginQrLogin");
  }

  std::future<nlohmann::json> PollQrLogin(std::string key) override {
    (void)key;
    return NativeAsyncNotImplemented("PollQrLogin");
  }

  std::future<nlohmann::json> SearchSongs(std::string keywords, int page, int pageSize) override {
    return std::async(
        std::launch::async,
        [keywords = std::move(keywords), page, pageSize] {
          SearchService search;
          return search.Search(keywords, "song", page, pageSize);
        });
  }

  std::future<nlohmann::json> GetPlaylistDetail(std::string id) override {
    (void)id;
    return NativeAsyncNotImplemented("GetPlaylistDetail");
  }

  std::future<nlohmann::json> ResolveSongUrl(std::string hash, std::string quality) override {
    return std::async(
        std::launch::async,
        [hash = std::move(hash), quality = std::move(quality)] {
          SongUrlService songUrl;
          return songUrl.Resolve(hash, quality);
        });
  }

  std::future<nlohmann::json> SearchLyrics(std::string hash) override {
    return std::async(std::launch::async, [hash = std::move(hash)] {
      LyricService lyric;
      return lyric.Search(hash);
    });
  }

  std::future<nlohmann::json> GetLyricDetail(std::string id, std::string accessKey) override {
    return std::async(
        std::launch::async,
        [id = std::move(id), accessKey = std::move(accessKey)] {
          LyricService lyric;
          return lyric.GetDetail(id, accessKey);
        });
  }

  std::future<AppSettings> LoadSettings() override {
    return std::async(std::launch::async, [databasePath = databasePath_] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();
      storage::SettingsRepository repository(database);
      return ToCoreSettings(repository.Load());
    });
  }

  std::future<void> SaveSettings(AppSettings settings) override {
    return std::async(std::launch::async, [databasePath = databasePath_, settings = std::move(settings)] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();
      storage::SettingsRepository repository(database);
      repository.Save(ToStorageSettings(settings));
    });
  }

 private:
  std::filesystem::path databasePath_;
};

}  // namespace

std::unique_ptr<IBackendFacade> CreateBackendFacade() {
  return CreateBackendFacade(storage::GetDefaultDatabasePath());
}

std::unique_ptr<IBackendFacade> CreateBackendFacade(std::filesystem::path databasePath) {
  return std::make_unique<BackendFacade>(std::move(databasePath));
}

}  // namespace echo::core

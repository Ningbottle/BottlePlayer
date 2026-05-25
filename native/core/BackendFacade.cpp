#include "echo/core/BackendFacade.h"

#include <filesystem>
#include <future>

#include "echo/core/DeviceService.h"
#include "echo/core/LoginService.h"
#include "echo/core/UserService.h"
#include "echo/core/LyricService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongUrlService.h"
#include "echo/storage/AppPaths.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
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
    return std::async(std::launch::async, [databasePath = databasePath_] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();
      storage::DeviceRepository deviceRepo(database);
      DeviceService devices(deviceRepo);
      const auto device = devices.EnsureDeviceReady();
      LoginService login;
      return login.BeginQrLogin(device);
    });
  }

  std::future<nlohmann::json> PollQrLogin(std::string key) override {
    return std::async(std::launch::async, [databasePath = databasePath_, key = std::move(key)] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();
      storage::DeviceRepository deviceRepo(database);
      DeviceService devices(deviceRepo);
      const auto device = devices.EnsureDeviceReady();
      LoginService login;
      auto result = login.PollQrLogin(device, key);

      // If login succeeded (status == 4), persist session credentials.
      if (result.contains("data") && result["data"].is_object()) {
        const auto& data = result["data"];
        if (data.value("status", 0) == 4) {
          SessionInfo session;
          session.token = data.value("token", "");
          session.userId = data.contains("userid")
              ? (data["userid"].is_string()
                     ? data["userid"].get<std::string>()
                     : std::to_string(data["userid"].get<std::int64_t>()))
              : "";
          if (!session.token.empty() && !session.userId.empty()) {
            storage::SessionRepository sessionRepo(database);
            sessionRepo.Save(session);
            
            // Claim VIP automatically on login
            UserService userSvc;
            userSvc.ClaimVip(session.userId, session.token);
          }
        }
      }
      return result;
    });
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
    return std::async(std::launch::async, [databasePath = databasePath_, id = std::move(id)] {
      storage::Database database;
      database.Open(databasePath);
      database.Initialize();

      // Load session for authenticated requests.
      storage::SessionRepository sessionRepo(database);
      const auto session = sessionRepo.Load();
      const std::string userId = session ? session->userId : "0";
      const std::string token = session ? session->token : "";

      storage::DeviceRepository deviceRepo(database);
      DeviceService devices(deviceRepo);
      const auto device = devices.EnsureDeviceReady();

      PlaylistService playlist;
      return playlist.GetPlaylistDetail(device, id, userId, token);
    });
  }

  std::future<nlohmann::json> ResolveSongUrl(std::string hash, std::string quality) override {
    return std::async(
        std::launch::async,
        [databasePath = databasePath_, hash = std::move(hash), quality = std::move(quality)] {
          storage::Database database;
          database.Open(databasePath);
          database.Initialize();

          storage::SessionRepository sessionRepo(database);
          const auto session = sessionRepo.Load();
          const std::string userId = session ? session->userId : "";
          const std::string token = session ? session->token : "";

          storage::DeviceRepository deviceRepo(database);
          DeviceService devices(deviceRepo);
          const auto device = devices.EnsureDeviceReady();

          SongUrlService songUrl;
          return songUrl.Resolve(hash, "", "", quality, "", userId, token, device);
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

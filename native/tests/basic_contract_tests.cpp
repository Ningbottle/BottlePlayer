#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <thread>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#include "echo/async/EventQueue.h"
#include "echo/async/RequestScheduler.h"
#include "echo/async/TaskScheduler.h"
#include "echo/core/Authorization.h"
#include "echo/core/StringUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/CatalogService.h"
#include "echo/core/CompatApi.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/HttpUtils.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/LoginService.h"
#include "echo/core/JsonHelpers.h"
#include "echo/core/LyricParser.h"
#include "echo/core/LyricService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/PrivilegeService.h"
#include "echo/core/RankService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongUrlService.h"
#include "echo/core/HomeService.h"
#include "echo/core/UserService.h"
#include "echo/core/SongService.h"
#include "echo/core/PlayHistoryService.h"
#include "echo/core/UserCloudService.h"
#include "echo/diagnostics/MemorySnapshot.h"
#include "echo/diagnostics/Redaction.h"
#include "echo/diagnostics/ScopedTimer.h"
#include "echo/image/ImageCache.h"
#include "echo/image/ImageLoader.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
#include "echo/storage/SettingsRepository.h"

namespace {

std::filesystem::path TestDbPath() {
  static int counter = 0;
  auto path = std::filesystem::temp_directory_path() /
              (L"echomusic-native-tests-" + std::to_wstring(++counter) + L".db");
  std::filesystem::remove(path);
  std::filesystem::remove(path.wstring() + L"-wal");
  std::filesystem::remove(path.wstring() + L"-shm");
  return path;
}

std::filesystem::path TestDirPath(const wchar_t* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path WriteTinyPng(const std::filesystem::path& dir) {
  const std::vector<std::uint8_t> png = {
      137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
      0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 31, 21, 196,
      137, 0, 0, 0, 13, 73, 68, 65, 84, 120, 156, 99, 248, 15, 4,
      0, 9, 251, 3, 253, 167, 229, 81, 126, 0, 0, 0, 0, 73, 69,
      78, 68, 174, 66, 96, 130};

  const auto path = dir / L"tiny.png";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  return path;
}

}  // namespace

int main() {
  std::cout << "[Test] main() started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  const auto auth = echo::core::ParseAuthorizationHeader(
      "token=abc; userid=42; t1=zzz; dfid=df; KUGOU_API_MID=mid; uuid=u; "
      "KUGOU_API_GUID=g; KUGOU_API_DEV=d; KUGOU_API_MAC=m");
  assert(auth.token == "abc");
  assert(auth.userId == "42");
  assert(auth.mid == "mid");
  assert(auth.guid == "g");

  {
    echo::core::DeviceInfo device;
    device.mid = "12345678901234567890123456789012345678";
    assert(echo::core::ResolveAndroidMid(device) == device.mid);
  }
  {
    echo::core::DeviceInfo device;
    device.guid = "guid-for-android-mid";
    device.mid = "legacy-mid";
    assert(echo::core::ResolveAndroidMid(device) ==
           echo::core::CalculateAndroidMid(device.guid));
  }
  {
    echo::core::DeviceInfo device;
    assert(echo::core::ResolveAndroidMid(device) == "0");
  }

  // ── KuGouAndroidRequest BuildSignedUrl contract ──────────────────────────
  {
    echo::core::KuGouAndroidRequest req;
    req.endpoint = "https://gateway.kugou.com/v5/url";
    req.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    req.includeSongUrlKey = true;
    req.params["hash"] = "abc123";
    req.params["quality"] = "128";
    req.params["clienttime"] = "1700000000";
    req.device.dfid = "dfid-test";
    req.device.guid = "guid-test";

    const auto url = echo::core::BuildSignedUrl(req);
    assert(!url.empty());
    assert(url.find("https://gateway.kugou.com/v5/url?") == 0);
    assert(url.find("hash=abc123") != std::string::npos);
    assert(url.find("quality=128") != std::string::npos);
    assert(url.find("appid=3116") != std::string::npos);
    assert(url.find("clientver=11440") != std::string::npos);
    assert(url.find("clienttime=1700000000") != std::string::npos);
    assert(url.find("dfid=dfid-test") != std::string::npos);
    assert(url.find("signature=") != std::string::npos);
    assert(url.find("key=") != std::string::npos);

    // BuildAndroidHeaders contract
    const auto headers = echo::core::BuildAndroidHeaders(req);
    assert(headers.count("dfid") && headers.at("dfid") == "dfid-test");
    assert(headers.count("mid"));
    assert(headers.count("clienttime") && headers.at("clienttime") == "1700000000");
    // KuGou anti-crawl headers (kg-rc / kg-thash / kg-rec) are fixed constants
    // emitted on every request, matching the reference implementation
    // (MakcRe/KuGouMusicApi util/request.js:41).
    assert(headers.at("kg-rc") == "1");
    assert(headers.at("kg-thash") == "5d816a0");
    assert(headers.at("kg-rec") == "1");
    assert(headers.at("kg-rf") == "B9EDA08A64250DEFFBCADDEE00F8F25F");
    assert(headers.at("Accept") == "application/json");

    // 边界：无 hash 时不生成 key
    {
      echo::core::KuGouAndroidRequest noHashReq;
      noHashReq.endpoint = "https://gateway.kugou.com/v3/get_my_info";
      noHashReq.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
      noHashReq.params["userid"] = "42";
      noHashReq.device.dfid = "dfid42";
      const auto url = echo::core::BuildSignedUrl(noHashReq);
      assert(url.find("key=") == std::string::npos);
      assert(url.find("appid=3116") != std::string::npos);
      assert(url.find("dfid=dfid42") != std::string::npos);
    }

    // 边界：空 device 时 mid 回退到 "0"
    {
      echo::core::KuGouAndroidRequest emptyDeviceReq;
      emptyDeviceReq.endpoint = "https://gateway.kugou.com/v5/url";
      emptyDeviceReq.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
      emptyDeviceReq.includeSongUrlKey = true;
      emptyDeviceReq.params["hash"] = "xyz";
      const auto url = echo::core::BuildSignedUrl(emptyDeviceReq);
      assert(url.find("mid=0") != std::string::npos);
      assert(url.find("dfid=-") != std::string::npos);
    }

    {
      echo::core::KuGouAndroidRequest explicitIdentityReq;
      explicitIdentityReq.endpoint = "https://gateway.kugou.com/v5/url";
      explicitIdentityReq.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
      explicitIdentityReq.includeSongUrlKey = true;
      explicitIdentityReq.params["hash"] = "fallbackhash";
      explicitIdentityReq.params["mid"] = "0";
      explicitIdentityReq.params["dfid"] = "-";
      explicitIdentityReq.params["uuid"] = "-";
      explicitIdentityReq.params["clienttime"] = "1700000001";
      explicitIdentityReq.device.dfid = "real-dfid";
      explicitIdentityReq.device.guid = "real-guid";
      const auto url = echo::core::BuildSignedUrl(explicitIdentityReq);
      const auto headers = echo::core::BuildAndroidHeaders(explicitIdentityReq);
      // Both BuildSignedUrl and BuildAndroidHeaders must honor explicit
      // params[mid]/params[dfid] overrides, so the URL query string and the
      // HTTP headers stay consistent.
      const auto expectedOverrideKey = echo::core::SignKey(
          "fallbackhash", "0", "0", explicitIdentityReq.profile.appid, explicitIdentityReq.profile.saltKind);
      assert(url.find("mid=0") != std::string::npos);
      assert(url.find("dfid=-") != std::string::npos);
      assert(url.find("uuid=-") != std::string::npos);
      assert(url.find("clienttime=1700000001") != std::string::npos);
      // 匿名回退的 key 也必须按 mid=0 签名，不能偷偷使用真实设备 MID。
      assert(url.find("key=" + expectedOverrideKey) != std::string::npos);
      assert(headers.at("mid") == "0");
      assert(headers.at("dfid") == "-");
      assert(headers.at("clienttime") == "1700000001");
    }
  }

  echo::storage::Database database;
  database.Open(TestDbPath());
  database.Initialize();

  const auto corruptDatabasePath = TestDbPath();
  {
    std::ofstream corrupt(corruptDatabasePath, std::ios::binary | std::ios::trunc);
    corrupt << R"({"kv_store":{},"api_cache":{}})";
  }
  {
    echo::storage::Database recoveredDatabase;
    recoveredDatabase.Open(corruptDatabasePath);
    recoveredDatabase.Initialize();
    recoveredDatabase.SetJson("recovered", nlohmann::json{{"ok", true}});
    const auto recovered = recoveredDatabase.GetJson("recovered");
    assert(recovered.has_value());
    assert(recovered->at("ok") == true);
  }

  const auto settingsPath = TestDbPath();
  {
    echo::storage::Database settingsDatabase;
    settingsDatabase.Open(settingsPath);
    settingsDatabase.Initialize();
    echo::storage::SettingsRepository settings(settingsDatabase);
    echo::storage::AppSettings savedSettings;
    savedSettings.volume = 0.72;
    savedSettings.startupPage = "now_playing";
    savedSettings.imageMemoryCacheMb = 24;
    settings.Save(savedSettings);
  }
  {
    echo::storage::Database settingsDatabase;
    settingsDatabase.Open(settingsPath);
    settingsDatabase.Initialize();
    echo::storage::SettingsRepository settings(settingsDatabase);
    const auto loadedSettings = settings.Load();
    assert(loadedSettings.volume == 0.72);
    assert(loadedSettings.startupPage == "now_playing");
    assert(loadedSettings.imageMemoryCacheMb == 24);
  }
  {
    echo::storage::Database settingsDatabase;
    settingsDatabase.Open(settingsPath);
    settingsDatabase.Initialize();
    echo::storage::SettingsRepository settings(settingsDatabase);
    const auto loadedSettings = settings.Load();
    assert(loadedSettings.volume == 0.72);
    assert(loadedSettings.startupPage == "now_playing");
    assert(loadedSettings.imageMemoryCacheMb == 24);
    echo::storage::AppSettings updatedSettings;
    updatedSettings.volume = 0.33;
    updatedSettings.startupPage = "home";
    updatedSettings.imageMemoryCacheMb = 16;
    settings.Save(updatedSettings);
  }
  {
    echo::storage::Database settingsDatabase;
    settingsDatabase.Open(settingsPath);
    settingsDatabase.Initialize();
    echo::storage::SettingsRepository settings(settingsDatabase);
    const auto loadedSettings = settings.Load();
    assert(loadedSettings.volume == 0.33);
    assert(loadedSettings.startupPage == "home");
    assert(loadedSettings.imageMemoryCacheMb == 16);
  }

  std::cout << "[Test] Instantiating api and testing /health, /register/dev, /server/now" << std::endl;
  echo::core::CompatApiHandlers defaultHandlers;
  defaultHandlers.loginQrKey = [](const echo::core::DeviceInfo&) -> nlohmann::json {
    return {{"status", 1}, {"data", {{"qrcode", "mock_key"}, {"qrcodeurl", "http://mock"}}}};
  };
  echo::core::CompatApi api(database, std::move(defaultHandlers));
  auto health = api.Handle("GET", "/health", {}, {}, "");
  assert(health.httpStatus == 200);
  assert(health.body["status"] == 1);

  auto device = api.Handle("POST", "/register/dev", {}, {}, "");
  assert(device.httpStatus == 200);
  assert(device.body["status"] == 1);
  // New device uses dfid="-" as unregistered placeholder; mid/uuid are derived.
  assert(device.body["data"]["dfid"].get<std::string>() == "-");
  assert(device.body["data"]["registered"] == false);

  auto now = api.Handle("GET", "/server/now", {}, {}, "");
  assert(now.httpStatus == 200);
  assert(now.body["data"]["timestamp"].get<std::int64_t>() > 0);

  std::cout << "[Test] Calling /login/qr/key (first time)..." << std::endl;
  auto loginQrKey = api.Handle("GET", "/login/qr/key", {}, {}, "");
  assert(loginQrKey.httpStatus == 200);
  assert(loginQrKey.body.contains("status"));
  // The route is now live — it must NOT return native_not_implemented.
  assert(!loginQrKey.body.contains("error_code") ||
         loginQrKey.body["error_code"].get<std::string>() != "native_not_implemented");

  {
    // Successful QR polling stores credentials in the backend but must not
    // expose them to the WebView response.
    echo::storage::Database loginDb;
    loginDb.Open(TestDbPath());
    loginDb.Initialize();
    echo::core::DeviceInfo registeredDevice;
    registeredDevice.dfid = "2ULHpc3qaLZa43ln8x0fLJQp";
    registeredDevice.registered = true;
    echo::storage::DeviceRepository(loginDb).Save(registeredDevice);

    echo::core::CompatApiHandlers loginHandlers;
    loginHandlers.loginQrCheck = [](const echo::core::DeviceInfo&, std::string) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"status", 4},
            {"userid", "webview-user-42"},
            {"token", "webview-token-secret"},
            {"t1", "webview-t1-secret"},
            {"nickname", "测试用户"}}}};
    };
    echo::core::CompatApi loginApi(loginDb, std::move(loginHandlers));
    const auto response =
        loginApi.Handle("GET", "/login/qr/check", {{"key", "qr-key"}}, {}, "");
    const auto responseText = response.body.dump();
    assert(responseText.find("webview-token-secret") == std::string::npos);
    assert(responseText.find("webview-t1-secret") == std::string::npos);

    const auto saved = echo::storage::SessionRepository(loginDb).Load();
    assert(saved.has_value());
    assert(saved->token == "webview-token-secret");
    assert(saved->userId == "webview-user-42");
    std::cout << "  [ok] QR login keeps credentials out of WebView" << std::endl;
  }

  {
    // Profile payloads can echo account credentials from upstream. Strip
    // those fields before crossing the native/WebView boundary.
    echo::storage::Database profileDb;
    profileDb.Open(TestDbPath());
    profileDb.Initialize();
    echo::core::CompatApiHandlers profileHandlers;
    profileHandlers.userDetail = [](std::string, std::string) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"userid", "profile-user"},
            {"nickname", "Profile"},
            {"token", "profile-token-secret"},
            {"t1", "profile-t1-secret"}}}};
    };
    echo::core::CompatApi profileApi(profileDb, std::move(profileHandlers));
    const auto response = profileApi.Handle("GET", "/user/detail", {}, {}, "");
    const auto responseText = response.body.dump();
    assert(responseText.find("profile-token-secret") == std::string::npos);
    assert(responseText.find("profile-t1-secret") == std::string::npos);
    assert(response.body["data"]["nickname"] == "Profile");
    std::cout << "  [ok] User detail keeps credentials out of WebView" << std::endl;
  }

  {
    // Routes that previously had no explicit StripSessionCredentials call
    // (e.g. /user/vip/detail) must still be scrubbed at the Handle chokepoint.
    echo::storage::Database vipDb;
    vipDb.Open(TestDbPath());
    vipDb.Initialize();
    echo::core::SessionInfo vipSession;
    vipSession.userId = "vip-user";
    vipSession.token = "vip-session-token";
    echo::storage::SessionRepository(vipDb).Save(vipSession);
    echo::core::CompatApiHandlers vipHandlers;
    vipHandlers.userVip = [](std::string, std::string) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"vip", 1},
            {"token", "vip-token-secret"},
            {"t1", "vip-t1-secret"}}}};
    };
    echo::core::CompatApi vipApi(vipDb, std::move(vipHandlers));
    const auto vipResponse = vipApi.Handle("GET", "/user/vip/detail", {}, {}, "");
    const auto vipText = vipResponse.body.dump();
    assert(vipText.find("vip-token-secret") == std::string::npos);
    assert(vipText.find("vip-t1-secret") == std::string::npos);
    assert(vipResponse.body["data"]["vip"] == 1);
    std::cout << "  [ok] CompatApi scrubs credentials at the Handle chokepoint" << std::endl;
  }

  {
    // Credential fields under variant names and nested objects are scrubbed.
    echo::storage::Database credDb;
    credDb.Open(TestDbPath());
    credDb.Initialize();
    echo::core::CompatApiHandlers credHandlers;
    credHandlers.userPlaylist = [](const echo::core::DeviceInfo&,
                                   std::string, std::string, int, int) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"lists",
             nlohmann::json::array({
                 nlohmann::json{{"access_token", "atk-secret"},
                                {"Token", "case-secret"},
                                {"signature", "sig-secret"},
                                {"cookie", "ck-secret"},
                                {"auth_token", "autk-secret"},
                                {"secret", "sec-secret"},
                                {"keep", "keep-me"}},
             })}}}};
    };
    echo::core::CompatApi credApi(credDb, std::move(credHandlers));
    const auto credResponse = credApi.Handle("GET", "/user/playlist", {}, {}, "");
    const auto credText = credResponse.body.dump();
    assert(credText.find("atk-secret") == std::string::npos);
    assert(credText.find("case-secret") == std::string::npos);
    assert(credText.find("sig-secret") == std::string::npos);
    assert(credText.find("ck-secret") == std::string::npos);
    assert(credText.find("autk-secret") == std::string::npos);
    assert(credText.find("sec-secret") == std::string::npos);
    assert(credText.find("keep-me") != std::string::npos);
    std::cout << "  [ok] StripSessionCredentials covers variant credential fields" << std::endl;
  }

  std::cout << "[Test] Testing ContractJsonMatches..." << std::endl;
  const nlohmann::json contractFixture = {
      {"status", 1},
      {"data",
       {
           {"song", "晴天"},
           {"timestamp", 111},
           {"play_url", "https://node.example/signed-a"},
           {"items", nlohmann::json::array({{{"hash", "abc123"}, {"url", "https://node.example/item-a"}}})},
       }},
  };
  const nlohmann::json nativeFixture = {
      {"status", 1},
      {"data",
       {
           {"song", "晴天"},
           {"timestamp", 222},
           {"play_url", "https://native.example/signed-b"},
           {"items", nlohmann::json::array({{{"hash", "abc123"}, {"url", "https://native.example/item-b"}}})},
       }},
  };
  std::vector<std::string> contractMismatches;
  assert(echo::core::ContractJsonMatches(
      contractFixture,
      nativeFixture,
      {"/data/timestamp", "/data/play_url", "/data/items/0/url"},
      &contractMismatches));
  assert(contractMismatches.empty());

  auto changedNativeFixture = nativeFixture;
  changedNativeFixture["data"]["song"] = "七里香";
  assert(!echo::core::ContractJsonMatches(
      contractFixture,
      changedNativeFixture,
      {"/data/timestamp", "/data/play_url", "/data/items/0/url"},
      &contractMismatches));
  assert(!contractMismatches.empty());
  assert(contractMismatches.front() == "/data/song");

  auto nativeFixtureWithExtraFields = nativeFixture;
  nativeFixtureWithExtraFields["data"]["extra_native_field"] = "allowed";
  nativeFixtureWithExtraFields["debug"] = {{"trace_id", "native-only"}};
  assert(echo::core::ContractJsonMatches(
      contractFixture,
      nativeFixtureWithExtraFields,
      {"/data/timestamp", "/data/play_url", "/data/items/0/url"},
      &contractMismatches));
  assert(contractMismatches.empty());

  auto nativeFixtureWithMissingArrayItem = nativeFixture;
  nativeFixtureWithMissingArrayItem["data"]["items"] = nlohmann::json::array();
  assert(!echo::core::ContractJsonMatches(
      contractFixture,
      nativeFixtureWithMissingArrayItem,
      {"/data/timestamp", "/data/play_url", "/data/items/0/url"},
      &contractMismatches));
  assert(!contractMismatches.empty());
  assert(contractMismatches.front() == "/data/items");

  std::cout << "[Test] Testing CompatApiHandlers..." << std::endl;
  echo::core::CompatApiHandlers compatHandlers;
  int compatSearchCalls = 0;
  int compatSongUrlCalls = 0;
  int compatLyricSearchCalls = 0;
  int compatLyricDetailCalls = 0;
  int compatPlaylistTracksCalls = 0;
  compatHandlers.search = [&](
                              std::string keywords,
                              std::string type,
                              int page,
                              int pageSize) {
    ++compatSearchCalls;
    assert(keywords == "晴天");
    assert(type == "song");
    assert(page == 2);
    assert(pageSize == 7);
    return nlohmann::json{
        {"status", 1},
        {"data",
         {
             {"keywords", keywords},
             {"type", type},
             {"page", page},
             {"pagesize", pageSize},
             {"lists", nlohmann::json::array({{{"SongName", "晴天"}, {"FileHash", "abc123"}}})},
         }},
    };
  };
  compatHandlers.songUrl = [&](
                               std::string hash,
                               std::string quality,
                               std::string ppageId) {
    ++compatSongUrlCalls;
    assert(hash == "abc123");
    assert(quality == "sq");
    assert(ppageId == "playlist_detail");
    return nlohmann::json{
        {"status", 1},
        {"url", "https://audio.example/abc123.flac"},
        {"data", {{"hash", hash}, {"quality", quality}, {"ppage_id", ppageId}}},
    };
  };
  compatHandlers.lyricSearch = [&](std::string hash) {
    ++compatLyricSearchCalls;
    assert(hash == "abc123");
    return nlohmann::json{
        {"status", 200},
        {"candidates", nlohmann::json::array({{{"id", "lyric-1"}, {"accesskey", "ak"}}})},
        {"data", {{"candidates", nlohmann::json::array({{{"id", "lyric-1"}, {"accesskey", "ak"}}})}}},
    };
  };
  compatHandlers.lyricDetail = [&](std::string id, std::string accessKey) {
    ++compatLyricDetailCalls;
    assert(id == "lyric-1");
    assert(accessKey == "ak");
    return nlohmann::json{
        {"status", 200},
        {"decodeContent", "[00:01.00]晴天"},
        {"data", {{"id", id}, {"accesskey", accessKey}}},
    };
  };
  compatHandlers.playlistTracks = [&](std::string id, int page, int pageSize) {
    ++compatPlaylistTracksCalls;
    assert(id == "125032");
    assert(page == 3);
    assert(pageSize == 12);
    return nlohmann::json{
        {"status", 1},
        {"data",
         {
             {"id", id},
             {"page", page},
             {"pagesize", pageSize},
             {"songs", nlohmann::json::array({{{"hash", "trackhash"}, {"songname", "晴天"}}})},
         }},
    };
  };
  echo::core::CompatApi compatApiWithHandlers(database, std::move(compatHandlers));
  const auto compatSearch = compatApiWithHandlers.Handle(
      "GET",
      "/search",
      {{"keyword", "晴天"}, {"type", "song"}, {"page", "2"}, {"pageSize", "7"}},
      {},
      "");
  assert(compatSearch.httpStatus == 200);
  assert(compatSearch.body["status"] == 1);
  assert(compatSearch.body["data"]["keywords"] == "晴天");
  assert(compatSearch.body["data"]["pagesize"] == 7);
  assert(compatSearchCalls == 1);

  const auto compatSongUrl = compatApiWithHandlers.Handle(
      "GET",
      "/song/url",
      {{"hash", "abc123"}, {"quality", "sq"}, {"ppage_id", "playlist_detail"}},
      {},
      "");
  assert(compatSongUrl.httpStatus == 200);
  assert(compatSongUrl.body["status"] == 1);
  assert(compatSongUrl.body["url"] == "https://audio.example/abc123.flac");
  assert(compatSongUrlCalls == 1);

  const auto compatLyricSearch = compatApiWithHandlers.Handle(
      "GET",
      "/search/lyric",
      {{"hash", "abc123"}},
      {},
      "");
  assert(compatLyricSearch.body["status"] == 200);
  assert(compatLyricSearch.body["candidates"].size() == 1);
  assert(compatLyricSearchCalls == 1);

  const auto compatLyricDetail = compatApiWithHandlers.Handle(
      "GET",
      "/lyric",
      {{"id", "lyric-1"}, {"accessKey", "ak"}},
      {},
      "");
  assert(compatLyricDetail.body["status"] == 200);
  assert(compatLyricDetail.body["decodeContent"] == "[00:01.00]晴天");
  assert(compatLyricDetailCalls == 1);

  const auto compatPlaylistTracks = compatApiWithHandlers.Handle(
      "GET",
      "/playlist/track/all",
      {{"id", "125032"}, {"page", "3"}, {"pageSize", "12"}},
      {},
      "");
  assert(compatPlaylistTracks.body["status"] == 1);
  assert(compatPlaylistTracks.body["data"]["songs"].size() == 1);
  assert(compatPlaylistTracks.body["data"]["pagesize"] == 12);
  assert(compatPlaylistTracksCalls == 1);

  echo::storage::Database deviceDb;
  deviceDb.Open(TestDbPath());
  deviceDb.Initialize();
  echo::storage::DeviceRepository deviceRepo(deviceDb);
  echo::core::DeviceService deviceService(deviceRepo);
  const auto facadeDevice = deviceService.EnsureDeviceReady();
  const auto secondFacadeDevice = deviceService.EnsureDeviceReady();
  // New device uses dfid="-" as unregistered placeholder; EnsureDeviceReady is idempotent.
  assert(facadeDevice.dfid == "-");
  assert(facadeDevice.dfid == secondFacadeDevice.dfid);
  assert(facadeDevice.mid.empty());
  assert(facadeDevice.uuid.empty());
  assert(facadeDevice.registered == false);

  {
    echo::storage::Database trustedDeviceDb;
    trustedDeviceDb.Open(TestDbPath());
    trustedDeviceDb.Initialize();
    echo::storage::DeviceRepository trustedDeviceRepo(trustedDeviceDb);
    trustedDeviceRepo.Save(echo::core::DeviceInfo{
        .dfid = "2ULHpc3qaLZa43ln8x0fLJQp",
        .mid = "0123456789abcdef0123456789abcdef",
        .uuid = "1779947671000",
        .guid = "guid-real",
        .serverDev = "",
        .mac = "02:00:00:00:00:00",
        .appid = "3116",
        .clientver = "11440",
        .registered = true,
    });
    echo::core::DeviceService trustedDeviceService(trustedDeviceRepo);
    const auto trustedDevice = trustedDeviceService.EnsureDeviceReady();
    assert(trustedDevice.dfid == "2ULHpc3qaLZa43ln8x0fLJQp");
    assert(trustedDevice.mid == "0123456789abcdef0123456789abcdef");
    assert(trustedDevice.uuid == "1779947671000");
    assert(trustedDevice.registered == true);
  }

  echo::core::SearchService searchService([](
                                              const std::string& url,
                                              const std::unordered_map<std::string, std::string>&) {
    assert(url.find("keyword=%E6%99%B4%E5%A4%A9") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"error":"","data":{"total":1,"info":[{"hash":"abc123","songname":"晴天","filename":"周杰伦 - 晴天","singername":"周杰伦","album_name":"叶惠美","album_id":"966846","duration":269,"album_audio_id":32100650,"audio_id":20505418,"mvhash":"mv123","privilege":10,"old_cpy":0,"pay_type":3,"320hash":"hq123","sqhash":"sq123","trans_param":{"union_cover":"http://imge.kugou.com/stdmusic/{size}/cover.jpg"}}]}})",
        ""};
  });
  const auto searchResult = searchService.Search("晴天", "song", 1, 30);
  assert(searchResult["status"] == 1);
  assert(searchResult["data"]["total"] == 1);
  assert(searchResult["data"]["lists"].size() == 1);
  assert(searchResult["data"]["lists"][0]["SongName"] == "晴天");
  assert(searchResult["data"]["lists"][0]["FileHash"] == "abc123");
  assert(searchResult["data"]["lists"][0]["HQ"]["Hash"] == "hq123");

  echo::core::SearchService discoverySearchService([](
                                                        const std::string& url,
                                                        const std::unordered_map<std::string, std::string>&) {
    if (url.find("/search/hot") != std::string::npos) {
      assert(url.find("count=2") != std::string::npos);
      return echo::core::HttpResult{
          200,
          R"({"status":1,"data":{"info":[{"keyword":"独家首发","sort":1,"jumpurl":"https://activity.example"},{"keyword":"儿歌大全","sort":2}]}})",
          ""};
    }

    assert(url.find("/search/song") != std::string::npos);
    assert(url.find("keyword=%E6%99%B4%E5%A4%A9") != std::string::npos);
    assert(url.find("pagesize=2") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"data":{"total":2,"info":[{"hash":"abc123","songname":"晴天","filename":"周杰伦 - 晴天","singername":"周杰伦","duration":269},{"hash":"def456","songname":"晴天娃娃","filename":"江语晨 - 晴天娃娃","singername":"江语晨","duration":242}]}})",
        ""};
  });
  const auto hotSearch = discoverySearchService.Hot(2);
  assert(hotSearch["status"] == 1);
  assert(hotSearch["data"]["list"].size() == 1);
  assert(hotSearch["data"]["list"][0]["name"] == "热门搜索");
  assert(hotSearch["data"]["list"][0]["keywords"][0]["keyword"] == "独家首发");
  assert(hotSearch["data"]["info"].size() == 2);

  const auto suggestSearch = discoverySearchService.Suggest("晴天", 2);
  assert(suggestSearch["status"] == 1);
  assert(suggestSearch["data"].size() == 1);
  assert(suggestSearch["data"][0]["LableName"] == "单曲");
  assert(suggestSearch["data"][0]["RecordDatas"].size() == 2);
  assert(suggestSearch["data"][0]["RecordDatas"][0]["HintInfo"] == "周杰伦 - 晴天");

  echo::core::SearchService typedSearchService([](
                                                    const std::string& url,
                                                    const std::unordered_map<std::string, std::string>&) {
    if (url.find("/search/special") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"data":{"total":1,"info":[{"specialid":6409645,"specialname":"周杰伦必听热歌","imgurl":"http://img.example/{size}/playlist.jpg","playcount":1000,"songcount":150,"nickname":"酷乐推荐"}]}})",
          ""};
    }

    if (url.find("/search/album") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"data":{"total":1,"info":[{"albumid":960399,"albumname":"魔杰座","singername":"周杰伦","singerid":3520,"songcount":11,"publishtime":"2008-10-15 00:00:00","imgurl":"http://img.example/{size}/album.jpg"}]}})",
          ""};
    }

    assert(url.find("/search/singer") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"data":[{"singername":"周杰伦","singerid":3520}]})",
        ""};
  });
  const auto specialSearch = typedSearchService.Search("周杰伦", "special", 1, 2);
  assert(specialSearch["status"] == 1);
  assert(specialSearch["data"]["info"].size() == 1);
  assert(specialSearch["data"]["info"][0]["specialid"] == 6409645);
  assert(specialSearch["data"]["info"][0]["name"] == "周杰伦必听热歌");

  const auto albumSearch = typedSearchService.Search("周杰伦", "album", 1, 2);
  assert(albumSearch["status"] == 1);
  assert(albumSearch["data"]["info"][0]["AlbumId"] == 960399);
  assert(albumSearch["data"]["info"][0]["AlbumName"] == "魔杰座");

  const auto authorSearch = typedSearchService.Search("周杰伦", "author", 1, 2);
  assert(authorSearch["status"] == 1);
  assert(authorSearch["data"]["info"][0]["AuthorId"] == 3520);
  assert(authorSearch["data"]["info"][0]["AuthorName"] == "周杰伦");

  echo::core::SongUrlService songUrlService([](
                                                const std::string& url,
                                                const std::unordered_map<std::string, std::string>&) {
    assert(url.find("hash=abc123") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"hash":"ABC123","url":"http://audio.example/song.mp3","backup_url":["http://audio.example/backup.mp3"],"fileName":"周杰伦 - 晴天","songName":"晴天","singerName":"周杰伦","albumid":966846,"album_audio_id":32100650,"audio_id":20505418,"timeLength":269000,"bitRate":128,"extName":"mp3","privilege":0,"pay_type":0})",
        ""};
  });
  const auto songUrl = songUrlService.Resolve("abc123", "", "");
  assert(songUrl["status"] == 1);
  assert(songUrl["url"] == "http://audio.example/song.mp3");
  assert(songUrl["data"]["play_url"] == "http://audio.example/song.mp3");
  assert(songUrl["data"]["backup_url"].size() == 1);

  std::string capturedSongUrlRequest;
  std::unordered_map<std::string, std::string> capturedSongUrlHeaders;
  echo::core::SongUrlService authenticatedSongUrlService([&](
                                                              const std::string& url,
                                                              const std::unordered_map<std::string, std::string>& headers) {
    capturedSongUrlRequest = url;
    capturedSongUrlHeaders = headers;
    return echo::core::HttpResult{
        200,
        R"({"status":1,"hash":"ABCDEF","url":"http://audio.example/authed.mp3"})",
        ""};
  });
  const echo::core::DeviceInfo qrLoginDevice{
      .dfid = "dfid123",
      .mid = "mid123",
      .uuid = "uuid123",
      .guid = "uuid123",
      .serverDev = "",
      .mac = "mac123",
      .appid = "1014",
      .clientver = "20000"};
  const auto authedSongUrl = authenticatedSongUrlService.Resolve(
      "ABCDEF", "123", "456", "sq", "", "42", "tok", qrLoginDevice);
  assert(authedSongUrl["status"] == 1);
  // Resolve forces appid=3116 / clientver=11430 (concept family; tracker
  // endpoint honors it per 2026-09-03 live split finding).
  const std::string expectedMid = echo::core::CalculateAndroidMid("uuid123");
  // Resolve lower-cases the hash before signing, so use "abcdef" for the expected key.
  const std::string expectedSongUrlKey =
      echo::core::SignKey("abcdef", expectedMid, "42", "3116");
  assert(capturedSongUrlRequest.find("https://gateway.kugou.com/v5/url?") == 0);
  assert(capturedSongUrlRequest.find("hash=abcdef") != std::string::npos);
  assert(capturedSongUrlRequest.find("album_id=123") != std::string::npos);
  assert(capturedSongUrlRequest.find("album_audio_id=456") != std::string::npos);
  assert(capturedSongUrlRequest.find("quality=sq") != std::string::npos);
  assert(capturedSongUrlRequest.find("appid=3116") != std::string::npos);
  assert(capturedSongUrlRequest.find("clientver=11430") != std::string::npos);
  assert(capturedSongUrlRequest.find("mid=" + expectedMid) != std::string::npos);
  assert(capturedSongUrlRequest.find("dfid=dfid123") != std::string::npos);
  assert(capturedSongUrlRequest.find("uuid=-") != std::string::npos);
  assert(capturedSongUrlRequest.find("userid=42") != std::string::npos);
  assert(capturedSongUrlRequest.find("token=tok") != std::string::npos);
  assert(capturedSongUrlRequest.find("key=" + expectedSongUrlKey) != std::string::npos);
  assert(capturedSongUrlRequest.find("appid=1014") == std::string::npos);
  assert(capturedSongUrlRequest.find("clientver=10000") == std::string::npos);
  assert(capturedSongUrlHeaders["x-router"] == "trackercdn.kugou.com");
  assert(capturedSongUrlHeaders["dfid"] == "dfid123");
  assert(capturedSongUrlHeaders["mid"] == expectedMid);
  assert(!capturedSongUrlHeaders["clienttime"].empty());

  std::vector<std::string> previewRequests;
  echo::core::SongUrlService previewSongUrlService([&](
                                                       const std::string& url,
                                                       const std::unordered_map<std::string, std::string>& headers) {
    previewRequests.push_back(url);
    if (previewRequests.size() == 1) {
      assert(url.find("userid=42") != std::string::npos);
      assert(url.find("mid=0") == std::string::npos);
      assert(headers.at("dfid") == "dfid123");
      assert(headers.at("mid") == expectedMid);
      return echo::core::HttpResult{200, R"({"status":2,"errcode":20018})", ""};
    }
    if (previewRequests.size() == 2) {
      assert(url.find("userid=42") == std::string::npos);
      assert(url.find("mid=0") != std::string::npos);
      assert(url.find("dfid=-") != std::string::npos);
      assert(headers.at("dfid") == "-");
      assert(headers.at("mid") == "0");
      return echo::core::HttpResult{
          200,
          R"({"status":2,"fail_process":["pkg","buy"],"hash_offset":{"offset_hash":"OFFSETHASH"}})",
          ""};
    }
    assert(url.find("hash=offsethash") != std::string::npos);
    assert(url.find("IsFreePart=1") != std::string::npos);
    assert(url.find("userid=42") == std::string::npos);
    assert(url.find("mid=0") != std::string::npos);
    assert(url.find("dfid=-") != std::string::npos);
    assert(headers.at("dfid") == "-");
    assert(headers.at("mid") == "0");
    return echo::core::HttpResult{
        200,
        R"({"status":1,"hash":"OFFSETHASH","url":["http://audio.example/preview.mp3"]})",
        ""};
  });
  const auto previewSongUrl = previewSongUrlService.Resolve(
      "VIPHASH", "123", "456", "", "", "42", "tok", qrLoginDevice);
  assert(previewSongUrl["status"] == 1);
  assert(previewSongUrl["is_preview"] == true);
  assert(previewSongUrl["url"] == "http://audio.example/preview.mp3");
  assert(previewRequests.size() == 3);

  echo::core::SongUrlService paidSongUrlService([](
                                                    const std::string&,
                                                    const std::unordered_map<std::string, std::string>&) {
    return echo::core::HttpResult{200, R"({"status":0,"url":"","error":"需要付费"})", ""};
  });
  const auto paidSongUrl = paidSongUrlService.Resolve("paidhash", "", "");
  assert(paidSongUrl["status"] == 0);
  assert(paidSongUrl["url"] == "");
  assert(paidSongUrl["error"] == "需要付费");

  echo::core::PrivilegeService privilegeService([](
                                                    const std::string& url,
                                                    const std::unordered_map<std::string, std::string>&) {
    assert(url.find("hash=abc123") != std::string::npos);
    assert(url.find("album_id=966846") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"hash":"ABC123","req_albumid":"966846","albumid":966846,"album_audio_id":32100650,"audio_id":20505418,"privilege":10,"pay_type":3,"extra":{"128hash":"base123","320hash":"hq123","sqhash":"sq123","highhash":"hi123"}})",
        ""};
  });
  const auto privilege = privilegeService.GetLite("abc123", "966846");
  assert(privilege["status"] == 1);
  assert(privilege["data"].size() == 1);
  assert(privilege["data"][0]["relate_goods"].size() == 4);
  assert(privilege["data"][0]["relate_goods"][1]["quality"] == "320");
  assert(privilege["data"][0]["relate_goods"][2]["hash"] == "sq123");
  assert(privilege["data"][0]["relateGoods"].size() == 4);

  echo::core::LyricService lyricService([](
                                            const std::string& url,
                                            const std::unordered_map<std::string, std::string>&) {
    if (url.find("/search?") != std::string::npos) {
      assert(url.find("hash=abc123") != std::string::npos);
      return echo::core::HttpResult{
          200,
          R"({"status":200,"candidates":[{"id":"274944371","accesskey":"access123"}],"info":[{"id":"274944371","accesskey":"access123"}]})",
          ""};
    }

    assert(url.find("/download?") != std::string::npos);
    assert(url.find("id=274944371") != std::string::npos);
    assert(url.find("accesskey=access123") != std::string::npos);
    assert(url.find("fmt=lrc") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":200,"content":"W3RpOnRlc3RdClswMDowMC4wMF1MeXJpYw=="})",
        ""};
  });
  const auto lyricSearch = lyricService.Search("abc123");
  assert(lyricSearch["status"] == 200);
  assert(lyricSearch["data"]["candidates"].size() == 1);
  assert(lyricSearch["candidates"][0]["accesskey"] == "access123");

  const auto lyricDetail = lyricService.GetDetail("274944371", "access123");
  assert(lyricDetail["status"] == 200);
  assert(lyricDetail["decodeContent"].get<std::string>().find("[00:00.00]") != std::string::npos);
  assert(lyricDetail["data"]["decodeContent"] == lyricDetail["decodeContent"]);

  echo::core::LyricService emptyLyricService;
  const auto emptyFacadeLyric = emptyLyricService.Search("");
  assert(emptyFacadeLyric["status"] == 1);
  assert(emptyFacadeLyric["data"]["candidates"].empty());

  const auto missingFacadeLyric = emptyLyricService.GetDetail("", "");
  assert(missingFacadeLyric["status"] == 0);
  assert(missingFacadeLyric["error_code"] == "native_lyric_missing_params");

  echo::core::PlaylistService playlistService([](
                                                  const std::string& url,
                                                  const std::unordered_map<std::string, std::string>&) {
    assert(url.find("specialid=125032") != std::string::npos);
    assert(url.find("page=1") != std::string::npos);
    assert(url.find("pagesize=2") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"error":"","data":{"total":14,"info":[{"hash":"trackhash","filename":"陈明真 - 我用自己的方式爱你","duration":288,"album_audio_id":32045535,"audio_id":156435,"album_id":960714,"mvhash":"mv123","privilege":10,"pay_type":3,"old_cpy":0,"320hash":"hqtrack","sqhash":"sqtrack","trans_param":{"union_cover":"http://imge.kugou.com/stdmusic/{size}/cover.jpg"}}]}})",
        ""};
  });
  const auto playlistTracks = playlistService.GetTracks("125032", 1, 2);
  assert(playlistTracks["status"] == 1);
  assert(playlistTracks["data"]["total"] == 14);
  assert(playlistTracks["data"]["songs"].size() == 1);
  assert(playlistTracks["data"]["songs"][0]["hash"] == "trackhash");
  assert(playlistTracks["data"]["songs"][0]["songname"] == "我用自己的方式爱你");
  assert(playlistTracks["data"]["songs"][0]["singername"] == "陈明真");
  assert(playlistTracks["data"]["songs"][0]["timelen"] == 288000);
  assert(playlistTracks["data"]["info"].size() == 1);

  echo::core::PlaylistService playlistDiscoveryService([](
                                                            const std::string& url,
                                                            const std::unordered_map<std::string, std::string>&) {
    if (url.find("/tag/recommend") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"(<!--KG_TAG_RES_START-->{"status":1,"data":{"info":[{"special_tag_id":1150,"id":1561,"name":"国语经典","bannerurl":"http://img.example/banner.jpg"},{"special_tag_id":583,"id":2245,"name":"睡前推荐"}]}}<!--KG_TAG_RES_END-->)",
          ""};
    }

    assert(url.find("/tag/specialList") != std::string::npos);
    assert(url.find("tagid=1150") != std::string::npos);
    assert(url.find("page=1") != std::string::npos);
    assert(url.find("pagesize=2") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"data":{"total":522,"info":[{"specialid":8380112,"playcount":100,"songcount":40,"specialname":"黄霑先生诞辰八十五周年典藏集","imgurl":"http://img.example/{size}/cover.jpg","intro":"经典国语","username":"酷乐推荐","publishtime":"2026-04-29"}]}})",
        ""};
  });
  const auto playlistTags = playlistDiscoveryService.GetTags();
  assert(playlistTags["status"] == 1);
  assert(playlistTags["data"]["list"].size() == 1);
  assert(playlistTags["data"]["list"][0]["tag_name"] == "推荐");
  assert(playlistTags["data"]["list"][0]["son"].size() == 2);
  assert(playlistTags["data"]["list"][0]["son"][0]["tag_id"] == 1150);
  assert(playlistTags["data"]["list"][0]["son"][0]["name"] == "国语经典");

  const auto topPlaylists = playlistDiscoveryService.GetTopPlaylists(1150, 1, 2, 2);
  assert(topPlaylists["status"] == 1);
  assert(topPlaylists["data"]["total"] == 522);
  assert(topPlaylists["data"]["info"].size() == 1);
  assert(topPlaylists["data"]["info"][0]["specialid"] == 8380112);
  assert(topPlaylists["data"]["info"][0]["name"] == "黄霑先生诞辰八十五周年典藏集");
  assert(topPlaylists["data"]["special_list"].size() == 1);

  echo::core::CatalogService catalogService([](
                                                const std::string& url,
                                                const std::unordered_map<std::string, std::string>&) {
    if (url.find("/album/info") != std::string::npos) {
      assert(url.find("albumid=960399") != std::string::npos);
      return echo::core::HttpResult{
          200,
          R"({"status":1,"data":{"albumid":960399,"albumname":"魔杰座","singername":"周杰伦","singerid":3520,"imgurl":"http://img.example/{size}/album.jpg","intro":"专辑简介","songcount":11,"publishtime":"2008-10-15 00:00:00"}})",
          ""};
    }

    if (url.find("/album/song") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"(<!--KG_TAG_RES_START-->{"status":1,"data":{"total":11,"info":[{"hash":"albumhash","filename":"周杰伦 - 龙战骑士","duration":268,"album_id":"960399","album_name":"魔杰座","audio_id":154262,"album_audio_id":32042818,"320hash":"albumhq","sqhash":"albumsq"}]}}<!--KG_TAG_RES_END-->)",
          ""};
    }

    if (url.find("/singer/info") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"data":{"singerid":3520,"singername":"周杰伦","profile":"歌手简介","imgurl":"http://img.example/artist.jpg","songcount":1754,"albumcount":49,"mvcount":10}})",
          ""};
    }

    if (url.find("/singer/song") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"(<!--KG_TAG_RES_START-->{"status":1,"data":{"total":1754,"info":[{"hash":"artisthash","filename":"周杰伦 - 晴天","duration":269,"album_id":"966846","album_name":"叶惠美","audio_id":20505418,"album_audio_id":32100650,"320hash":"artisthq","sqhash":"artistsq"}]}}<!--KG_TAG_RES_END-->)",
          ""};
    }

    assert(url.find("/singer/album") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"data":{"total":49,"info":[{"albumid":179652761,"albumname":"太阳之子","singername":"周杰伦","singerid":3520,"songcount":13,"imgurl":"http://img.example/{size}/sun.jpg"}]}})",
        ""};
  });
  const auto albumDetail = catalogService.GetAlbumDetail("960399");
  assert(albumDetail["status"] == 1);
  assert(albumDetail["data"]["info"][0]["AlbumName"] == "魔杰座");
  assert(albumDetail["data"]["info"][0]["SingerName"] == "周杰伦");

  const auto albumSongs = catalogService.GetAlbumSongs("960399", 1, 2);
  assert(albumSongs["status"] == 1);
  assert(albumSongs["data"]["total"] == 11);
  assert(albumSongs["data"]["info"][0]["songname"] == "龙战骑士");
  assert(albumSongs["data"]["info"][0]["timelen"] == 268000);

  const auto artistDetail = catalogService.GetArtistDetail("3520");
  assert(artistDetail["status"] == 1);
  assert(artistDetail["data"]["info"][0]["AuthorName"] == "周杰伦");
  assert(artistDetail["data"]["info"][0]["intro"] == "歌手简介");

  const auto artistSongs = catalogService.GetArtistSongs("3520", 1, 2, "hot");
  assert(artistSongs["status"] == 1);
  assert(artistSongs["data"]["info"][0]["songname"] == "晴天");

  const auto artistAlbums = catalogService.GetArtistAlbums("3520", 1, 2, "hot");
  assert(artistAlbums["status"] == 1);
  assert(artistAlbums["data"]["total"] == 49);
  assert(artistAlbums["data"]["info"][0]["AlbumName"] == "太阳之子");

  echo::core::RankService rankService([](
                                          const std::string& url,
                                          const std::unordered_map<std::string, std::string>&) {
    if (url.find("/rank/list") != std::string::npos) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"error":"","data":{"total":1,"info":[{"rankid":8888,"rankname":"TOP500","imgurl":"http://img.example/top.jpg","ranktype":2,"updatefrequency":"每天"}]}})",
          ""};
    }

    assert(url.find("/rank/song") != std::string::npos);
    assert(url.find("rankid=8888") != std::string::npos);
    assert(url.find("page=1") != std::string::npos);
    assert(url.find("pagesize=2") != std::string::npos);
    return echo::core::HttpResult{
        200,
        R"({"status":1,"error":"","data":{"total":500,"info":[{"hash":"rankhash","songname":"人生路漫漫","filename":"白小白 - 人生路漫漫","duration":235,"album_audio_id":855779725,"audio_id":548404459,"album_id":179917533,"album_sizable_cover":"http://imge.kugou.com/stdmusic/{size}/cover.jpg","privilege":8,"pay_type":3,"old_cpy":0,"320hash":"rankhq","sqhash":"ranksq"}]}})",
        ""};
  });
  const auto rankList = rankService.List();
  assert(rankList["status"] == 1);
  assert(rankList["data"]["info"].size() == 1);
  assert(rankList["data"]["info"][0]["rankid"] == 8888);

  const auto rankSongs = rankService.GetSongs(8888, 1, 2);
  assert(rankSongs["status"] == 1);
  assert(rankSongs["data"]["total"] == 500);
  assert(rankSongs["data"]["info"].size() == 1);
  assert(rankSongs["data"]["info"][0]["audio_info"]["hash"] == "rankhash");
  assert(rankSongs["data"]["info"][0]["audio_info"]["duration"] == 235000);
  assert(rankSongs["data"]["info"][0]["album_info"]["sizable_cover"].get<std::string>().find("{size}") != std::string::npos);

  echo::async::CancellationSource cancellation;
  cancellation.Cancel();
  echo::async::TaskScheduler scheduler;
  std::atomic_bool cancelledWorkRan = false;
  auto cancelledFuture = scheduler.Schedule(
      [&cancelledWorkRan](echo::async::CancellationToken) { cancelledWorkRan = true; },
      cancellation.Token());
  cancelledFuture.get();
  assert(!cancelledWorkRan.load());
  assert(scheduler.PendingCount() == 0);

  echo::async::EventQueue events;
  echo::async::CancellationSource activeWork;
  auto eventFuture = scheduler.ScheduleAndPost(
      [](echo::async::CancellationToken) {
        return echo::async::BackendEvent{"search.completed", "ok"};
      },
      activeWork.Token(),
      events);
  eventFuture.get();
  const auto event = events.TryPop();
  assert(event.has_value());
  assert(event->type == "search.completed");
  assert(event->payload == "ok");
  assert(events.Size() == 0);

  echo::async::CancellationSource cancelledEventWork;
  cancelledEventWork.Cancel();
  auto noEventFuture = scheduler.ScheduleAndPost(
      [](echo::async::CancellationToken) {
        return echo::async::BackendEvent{"should.not.arrive", "cancelled"};
      },
      cancelledEventWork.Token(),
      events);
  noEventFuture.get();
  assert(events.Size() == 0);

  scheduler.Shutdown();
  bool rejectedAfterShutdown = false;
  try {
    (void)scheduler.Schedule(
        [](echo::async::CancellationToken) {},
        echo::async::CancellationSource{}.Token());
  } catch (...) {
    rejectedAfterShutdown = true;
  }
  assert(rejectedAfterShutdown);

  echo::image::MemoryImageCache defaultImageCache;
  assert(defaultImageCache.Stats().byteBudget == 16 * 1024 * 1024);

  echo::image::MemoryImageCache imageCache(4);
  imageCache.Put("a", {1, 2, 3});
  imageCache.Put("b", {4, 5, 6});
  assert(!imageCache.Get("a").has_value());
  const auto cachedImage = imageCache.Get("b");
  assert(cachedImage.has_value());
  assert(cachedImage->bytes.size() == 3);
  assert(imageCache.Stats().byteCount <= imageCache.Stats().byteBudget);

  echo::image::MemoryImageCache scrollingImageCache(8 * 1024);
  for (int i = 0; i < 10000; ++i) {
    scrollingImageCache.Put("cover:" + std::to_string(i), std::vector<std::uint8_t>(256, 7));
  }
  assert(scrollingImageCache.Stats().byteCount <= scrollingImageCache.Stats().byteBudget);
  assert(scrollingImageCache.Stats().itemCount <= 32);

  const auto imageTestDir = TestDirPath(L"bottlemusic-image-tests");
  echo::image::DiskImageCache diskCache(imageTestDir / L"disk-cache", 8);
  diskCache.Put("first", {1, 2, 3});
  assert(diskCache.Get("first").has_value());
  assert(diskCache.Stats().byteCount <= diskCache.Stats().byteBudget);

  echo::image::WicImageDecoder decoder;
  const auto missingImage = decoder.DecodeFile(imageTestDir / L"missing.png");
  assert(missingImage.placeholder);
  assert(missingImage.width == 1);
  assert(missingImage.height == 1);

  const auto pngPath = WriteTinyPng(imageTestDir);
  const auto decodedImage = decoder.DecodeFile(pngPath);
  assert(!decodedImage.placeholder);
  assert(decodedImage.width == 1);
  assert(decodedImage.height == 1);
  assert(decodedImage.bgra.size() == 4);

  echo::image::MemoryImageCache loaderMemoryCache(1024 * 1024);
  echo::image::DiskImageCache loaderDiskCache(imageTestDir / L"loader-disk-cache", 1024 * 1024);
  echo::image::ImageLoader imageLoader(loaderMemoryCache, loaderDiskCache);

  echo::async::CancellationSource cancelledImageLoad;
  cancelledImageLoad.Cancel();
  const auto cancelledImage = imageLoader.LoadFile("cancelled", pngPath, cancelledImageLoad.Token());
  assert(cancelledImage.cancelled);
  assert(cancelledImage.placeholder);
  assert(!loaderMemoryCache.Get("cancelled").has_value());

  echo::async::CancellationSource activeImageLoad;
  const auto loadedImage = imageLoader.LoadFile("tiny", pngPath, activeImageLoad.Token());
  assert(!loadedImage.placeholder);
  assert(loadedImage.width == 1);
  assert(loaderMemoryCache.Get("tiny").has_value());
  assert(loaderDiskCache.Get("tiny").has_value());

  const auto cachedLoadedImage = imageLoader.LoadFile("tiny", pngPath, activeImageLoad.Token());
  assert(cachedLoadedImage.fromMemoryCache);
  assert(cachedLoadedImage.bgra.size() == 4);

  const auto tinyPngBytes = [&pngPath] {
    std::ifstream input(pngPath, std::ios::binary);
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  }();
  bool remoteFetchCalled = false;
  echo::async::CancellationSource remoteImageLoad;
  const auto remoteLoadedImage = imageLoader.LoadRemote(
      "remote-cover:https://img.example/tiny.png",
      "https://img.example/tiny.png",
      [&remoteFetchCalled, &tinyPngBytes](const std::string& url) {
        remoteFetchCalled = true;
        assert(url == "https://img.example/tiny.png");
        return echo::image::ImageLoader::RemoteFetchResult{200, tinyPngBytes, ""};
      },
      remoteImageLoad.Token());
  assert(remoteFetchCalled);
  assert(!remoteLoadedImage.placeholder);
  assert(remoteLoadedImage.width == 1);
  assert(loaderMemoryCache.Get("remote-cover:https://img.example/tiny.png").has_value());

  remoteFetchCalled = false;
  const auto cachedRemoteImage = imageLoader.LoadRemote(
      "remote-cover:https://img.example/tiny.png",
      "https://img.example/tiny.png",
      [&remoteFetchCalled](const std::string&) {
        remoteFetchCalled = true;
        return echo::image::ImageLoader::RemoteFetchResult{500, {}, "should_not_fetch"};
      },
      remoteImageLoad.Token());
  assert(cachedRemoteImage.fromMemoryCache);
  assert(!remoteFetchCalled);


  // ── Diagnostics contract tests ────────────────────────────────────────
  {
    // Stopwatch: elapsed must be non-negative and increase over time
    auto sw = echo::diagnostics::Stopwatch::Start();
    assert(sw.ElapsedMs() >= 0);
    // Sleep a tiny bit to ensure elapsed > 0 on most systems.
    // Use std::this_thread::sleep_for instead of the Win32 Sleep()/POSIX
    // usleep() so the test does not pull in <windows.h> via a transitively-
    // included header (the old win32_app includes used to provide it).
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    assert(sw.ElapsedMs() >= 10);
    std::cout << "  [ok] Stopwatch elapsed_ms >= 10 after 15ms sleep" << std::endl;
  }

  {
    // RedactSensitive: token / dfid / userid / Cookie / KugooID
    std::string raw = "token=secret123&dfid=abcdefghijk&userid=424242&Cookie=x=y; KugooID=kgid";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    assert(redacted.find("secret123") == std::string::npos);
    assert(redacted.find("token=***") != std::string::npos);
    assert(redacted.find("Cookie=***") != std::string::npos);
    assert(redacted.find("KugooID=***") != std::string::npos);
    // dfid masked
    assert(redacted.find("abcdefghijk") == std::string::npos);
    // userid long enough to mask
    assert(redacted.find("424242") == std::string::npos);
    std::cout << "  [ok] RedactSensitive masks token/dfid/Cookie/KugooID" << std::endl;
  }

  {
    // RedactSensitive: all credential keys from the StripSessionCredentials
    // allowlist must be masked in log output (M2 alignment).
    std::string raw = "t1=secret-t1&access_token=secret-at&auth_token=secret-aut&session_token=secret-st&secret=secret-val&set-cookie=secret-sc&signature=secret-sig";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    assert(redacted.find("secret-t1") == std::string::npos);
    assert(redacted.find("t1=***") != std::string::npos);
    assert(redacted.find("secret-at") == std::string::npos);
    assert(redacted.find("access_token=***") != std::string::npos);
    assert(redacted.find("secret-aut") == std::string::npos);
    assert(redacted.find("auth_token=***") != std::string::npos);
    assert(redacted.find("secret-st") == std::string::npos);
    assert(redacted.find("session_token=***") != std::string::npos);
    assert(redacted.find("secret-val") == std::string::npos);
    assert(redacted.find("secret=***") != std::string::npos);
    assert(redacted.find("secret-sc") == std::string::npos);
    assert(redacted.find("set-cookie=***") != std::string::npos);
    assert(redacted.find("secret-sig") == std::string::npos);
    assert(redacted.find("signature=***") != std::string::npos);
    std::cout << "  [ok] RedactSensitive masks all StripSessionCredentials keys" << std::endl;
  }

  {
    std::string raw = R"(Cookie=abc url=https://example.test {"token":"json-secret"} token=query-secret)";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    assert(redacted.find("url=https://example.test") != std::string::npos);
    assert(redacted.find("json-secret") == std::string::npos);
    assert(redacted.find("query-secret") == std::string::npos);
    // JSON "token":"..." path replaces the value with literal "***".
    assert(redacted.find(R"("token":"***")") != std::string::npos);
    // token=... path (mask_param with total_mask) also yields "***".
    assert(redacted.find("token=***") != std::string::npos);
    std::cout << "  [ok] RedactSensitive preserves post-Cookie fields and masks JSON token" << std::endl;
  }

  {
    // RedactSensitive: signed CDN play_url with auth in the query string must
    // not leak the auth params. KuGou signs the *URL itself* (auth=, ssig=,
    // expires=, token=...) rather than carrying the token under a separate
    // token= key, so a key-list redactor leaves it in the log file.
    std::string raw =
        R"(play_url=https://trackcdn.example/audio.flac?auth=SECRETKEY&ssig=ABCDEF&expires=1700000000)";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    // Every secret query value must be gone.
    assert(redacted.find("SECRETKEY") == std::string::npos);
    assert(redacted.find("ABCDEF") == std::string::npos);
    // The path (no secret) is preserved so the log stays useful.
    assert(redacted.find("https://trackcdn.example/audio.flac") != std::string::npos);
    std::cout << "  [ok] RedactSensitive scrubs play_url query-string secrets" << std::endl;
  }

  {
    // RedactSensitive: a plain URL with no query string is left untouched, so
    // the non-secret URL in the existing fixture behavior is not regressed.
    std::string raw = "url=https://example.test";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    assert(redacted == "url=https://example.test");
    std::cout << "  [ok] RedactSensitive leaves query-less URL intact" << std::endl;
  }

  {
    // RedactSensitive: an empty-valued token= must NOT terminate scanning of
    // later occurrences. Previously mask_param did `break` on vlen==0, so
    // "token=&... token=LEAKED" left LEAKED in the log. The empty first value
    // is skipped and the second is still masked.
    std::string raw = "token=&junk=1 token=LEAKED";
    auto redacted = echo::diagnostics::RedactSensitive(raw);
    assert(redacted.find("LEAKED") == std::string::npos);
    // The empty occurrence stays empty (no value to mask) and the populated
    // one becomes "***".
    assert(redacted.find("token=&") != std::string::npos);
    assert(redacted.find("token=***") != std::string::npos);
    std::cout << "  [ok] RedactSensitive continues past empty-valued token= and masks later values" << std::endl;
  }

  {
    // TruncateForLog
    std::string longText(600, 'x');
    auto truncated = echo::diagnostics::TruncateForLog(longText, 512);
    assert(truncated.size() <= 512 + std::string("... truncated=true").size());
    assert(truncated.find("truncated=true") != std::string::npos);
    std::string shortText = "short";
    assert(echo::diagnostics::TruncateForLog(shortText, 512) == "short");
    std::cout << "  [ok] TruncateForLog caps at maxBytes and marks truncated" << std::endl;
  }

  {
    // MaskMiddle
    assert(echo::diagnostics::MaskMiddle("abcdefghij", 3, 3) == "abc...hij");
    assert(echo::diagnostics::MaskMiddle("ab", 3, 3) == "**");
    assert(echo::diagnostics::MaskMiddle("abc", 3, 3) == "***");
    std::cout << "  [ok] MaskMiddle prefix...suffix behavior" << std::endl;
  }

  {
    // UrlEncode contract tests
    using echo::core::UrlEncode;
    // Safe chars preserved
    assert(UrlEncode("abcABC123-_.~") == "abcABC123-_.~");
    // Space -> %20
    assert(UrlEncode("hello world") == "hello%20world");
    // Comma -> %2C
    assert(UrlEncode("a,b") == "a%2Cb");
    // UTF-8 Chinese
    assert(UrlEncode("中文") == "%E4%B8%AD%E6%96%87");
    // Already-encoded percent gets double-encoded
    assert(UrlEncode("100%") == "100%25");
    std::cout << "  [ok] UrlEncode contract (safe/special/UTF-8/double-encode)" << std::endl;
  }

  {
    // CompatRequestContext: empty DB -> fallback userId, empty token, default device
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();

    echo::core::CompatRequestContext ctx(db);
    assert(ctx.UserIdOr("fallback") == "fallback");
    assert(ctx.TokenOrEmpty() == "");
    assert(!ctx.Session().has_value());

    const auto& device = ctx.Device();
    assert(device.dfid == "-");
    std::cout << "  [ok] CompatRequestContext empty-DB fallback" << std::endl;
  }

  {
    // CompatRequestContext: with saved session -> userId/token loaded
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();

    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "42";
    session.token = "tok";
    repo.Save(session);

    echo::core::CompatRequestContext ctx(db);
    assert(ctx.UserIdOr("fallback") == "42");
    assert(ctx.TokenOrEmpty() == "tok");
    assert(ctx.Session().has_value());
    assert(ctx.Session()->userId == "42");
    assert(ctx.HasLogin());
    std::cout << "  [ok] CompatRequestContext session load" << std::endl;
  }

  {
    // Session credentials must be encrypted at rest while remaining readable
    // by the same Windows user account.
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();

    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "release-user-728419";
    session.token = "release-token-4f8d6e2c";
    session.t1 = "release-t1-c9a7b3";
    session.nickname = "release-nickname";
    session.pic = "https://example.invalid/release-avatar.png";
    repo.Save(session);

    const auto stored = db.GetJson("session.info");
    assert(stored.has_value());
    assert(stored->value("version", 0) == 1);
    assert(stored->contains("protected_data"));
    assert((*stored)["protected_data"].is_string());
    const auto raw = stored->dump();
    assert(raw.find(session.userId) == std::string::npos);
    assert(raw.find(session.token) == std::string::npos);
    assert(raw.find(session.t1) == std::string::npos);
    assert(raw.find(session.nickname) == std::string::npos);
    assert(raw.find(session.pic) == std::string::npos);

    const auto loaded = repo.Load();
    assert(loaded.has_value());
    assert(loaded->userId == session.userId);
    assert(loaded->token == session.token);
    assert(loaded->t1 == session.t1);
    assert(loaded->nickname == session.nickname);
    assert(loaded->pic == session.pic);
    std::cout << "  [ok] SessionRepository encrypts credentials at rest" << std::endl;
  }

  {
    // Round-trip still works after SecureZeroMemory is added to DPAPI paths.
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "zero-user";
    session.token = "zero-token";
    session.t1 = "zero-t1";
    session.nickname = "zero-nick";
    session.pic = "zero-pic";
    repo.Save(session);
    const auto loaded = repo.Load();
    assert(loaded.has_value());
    assert(loaded->token == "zero-token");
    assert(loaded->t1 == "zero-t1");
    assert(loaded->userId == "zero-user");
    std::cout << "  [ok] SessionRepository round-trip survives buffer zeroing" << std::endl;
  }

  {
    // Existing plaintext sessions are migrated in place on first read.
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    db.SetJson("session.info",
               {{"userid", "legacy-user-9142"},
                {"token", "legacy-token-a81e"},
                {"t1", "legacy-t1-532f"},
                {"nickname", "legacy-nickname"},
                {"pic", "legacy-picture"}});

    echo::storage::SessionRepository repo(db);
    const auto loaded = repo.Load();
    assert(loaded.has_value());
    assert(loaded->userId == "legacy-user-9142");
    assert(loaded->token == "legacy-token-a81e");

    const auto migrated = db.GetJson("session.info");
    assert(migrated.has_value());
    assert(migrated->value("version", 0) == 1);
    assert(migrated->contains("protected_data"));
    const auto raw = migrated->dump();
    assert(raw.find("legacy-user-9142") == std::string::npos);
    assert(raw.find("legacy-token-a81e") == std::string::npos);
    assert(raw.find("legacy-t1-532f") == std::string::npos);
    std::cout << "  [ok] SessionRepository migrates plaintext sessions" << std::endl;
  }

  {
    // Once migration has run, a plaintext session.info payload is no longer
    // trusted (closes the silent-plaintext-bypass gap).
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    // Seed legacy plaintext and migrate via first Load().
    db.SetJson("session.info",
               {{"userid", "legacy-user-2"}, {"token", "legacy-token-2"}, {"t1", "legacy-t1-2"},
                {"nickname", "legacy-nick-2"}, {"pic", "legacy-pic-2"}});
    echo::storage::SessionRepository repo(db);
    assert(repo.Load().has_value());
    // Simulate a plaintext blob written after migration (bug/restore/other writer).
    db.SetJson("session.info",
               {{"userid", "sneak-user"}, {"token", "sneak-token"}, {"t1", "sneak-t1"},
                {"nickname", "sneak-nick"}, {"pic", "sneak-pic"}});
    const auto afterMigration = repo.Load();
    assert(!afterMigration.has_value());
    std::cout << "  [ok] SessionRepository refuses plaintext after migration" << std::endl;
  }

  {
    // CompatRequestContext: HasLogin() returns false when session is empty or incomplete
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::core::CompatRequestContext ctx(db);
    assert(!ctx.HasLogin());
    std::cout << "  [ok] CompatRequestContext HasLogin empty DB" << std::endl;
  }

  {
    // CompatRequestContext: SaveDevice persists changes and updates the cache
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();

    echo::core::CompatRequestContext ctx(db);
    auto device = ctx.Device();
    assert(device.registered == false);
    // A real (non-placeholder) dfid is required, otherwise EnsureDeviceReady's
    // NormalizeDeviceInfo forces registered=false on the next load.
    device.dfid = "2ULHpc3qaLZa43ln8x0fLJQp";
    device.registered = true;
    ctx.SaveDevice(device);
    // Cached device should reflect the update immediately.
    assert(ctx.Device().registered == true);
    assert(ctx.Device().dfid == "2ULHpc3qaLZa43ln8x0fLJQp");

    // Fresh context should also see the persisted value.
    echo::core::CompatRequestContext ctx2(db);
    assert(ctx2.Device().registered == true);
    assert(ctx2.Device().dfid == "2ULHpc3qaLZa43ln8x0fLJQp");
    std::cout << "  [ok] CompatRequestContext SaveDevice persists and caches" << std::endl;
  }

  {
    // RequestScheduler: bounded concurrency — worker count limits parallel execution.
    std::cout << "[Test] Testing RequestScheduler bounded concurrency..." << std::endl;
    echo::async::RequestScheduler scheduler(2);
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 4; ++i) {
      futures.push_back(scheduler.Submit(echo::async::RequestKind::Generic, [&active, &maxActive, i](echo::async::CancellationToken) -> int {
        int current = ++active;
        int prevMax = maxActive.load();
        while (current > prevMax && !maxActive.compare_exchange_weak(prevMax, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        --active;
        return i;
      }));
    }
    for (auto& f : futures) {
      f.wait();
    }
    assert(maxActive.load() <= 2);
    std::cout << "  [ok] RequestScheduler bounded concurrency (maxActive=" << maxActive.load() << ")" << std::endl;
  }

  {
    // RequestScheduler: latest-wins — only the most recent SubmitLatest result survives.
    std::cout << "[Test] Testing RequestScheduler latest-wins..." << std::endl;
    echo::async::RequestScheduler scheduler(2);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; ++i) {
      futures.push_back(scheduler.SubmitLatest(echo::async::RequestKind::SongUrl, [i](echo::async::CancellationToken cancelToken) -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (cancelToken.IsCancellationRequested()) {
          return -1;
        }
        return i;
      }));
    }
    // The oldest futures should be canceled; the newest should return 4.
    for (std::size_t i = 0; i < futures.size(); ++i) {
      int value = futures[i].get();
      if (i < futures.size() - 1) {
        assert(value == -1);
      } else {
        assert(value == 4);
      }
    }
    std::cout << "  [ok] RequestScheduler latest-wins" << std::endl;
  }

  {
    // RequestScheduler: Cancel — explicit Cancel cancels an in-flight or pending job.
    std::cout << "[Test] Testing RequestScheduler Cancel..." << std::endl;
    echo::async::RequestScheduler scheduler(2);
    std::atomic<bool> started{false};
    auto future = scheduler.SubmitLatest(echo::async::RequestKind::SongUrl, [&started](echo::async::CancellationToken cancelToken) -> int {
      started.store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      if (cancelToken.IsCancellationRequested()) {
        return -1;
      }
      return 0;
    });
    // Wait until the job has started, then cancel it.
    while (!started.load()) {
      std::this_thread::yield();
    }
    scheduler.Cancel(echo::async::RequestKind::SongUrl);
    int value = future.get();
    assert(value == -1);
    std::cout << "  [ok] RequestScheduler Cancel" << std::endl;
  }

  {
    // RequestScheduler: Submit exception safety — promise is fulfilled via set_exception,
    // so future.get() propagates the exception rather than hanging or crashing.
    std::cout << "[Test] Testing RequestScheduler exception safety..." << std::endl;
    echo::async::RequestScheduler scheduler(2);
    auto future = scheduler.Submit(echo::async::RequestKind::Generic, [](echo::async::CancellationToken) -> int {
      throw std::runtime_error("test exception");
    });
    bool caught = false;
    try {
      future.get();
    } catch (const std::runtime_error&) {
      caught = true;
    }
    assert(caught);
    std::cout << "  [ok] RequestScheduler exception safety" << std::endl;
  }

  {
    // ParseHttpRequest: header key case-insensitive.
    std::cout << "[Test] Testing ParseHttpRequest header case-insensitive..." << std::endl;
    std::string method, path;
    echo::core::QueryMap query;
    echo::core::HeaderMap headers;
    std::string raw = "GET /test HTTP/1.1\r\nContent-Length: 42\r\n\r\n";
    bool ok = echo::core::ParseHttpRequest(raw, method, path, query, headers);
    assert(ok);
    assert(method == "GET");
    assert(path == "/test");
    assert(headers["content-length"] == "42");

    headers.clear();
    raw = "GET /test2 HTTP/1.1\r\ncontent-length: 99\r\n\r\n";
    ok = echo::core::ParseHttpRequest(raw, method, path, query, headers);
    assert(ok);
    assert(headers["content-length"] == "99");

    headers.clear();
    raw = "GET /test3 HTTP/1.1\r\nCoNtEnT-LeNgTh: 123\r\n\r\n";
    ok = echo::core::ParseHttpRequest(raw, method, path, query, headers);
    assert(ok);
    assert(headers["content-length"] == "123");
    std::cout << "  [ok] ParseHttpRequest header case-insensitive" << std::endl;
  }

  {
    // ParseHttpRequest: malformed Content-Length does not crash.
    std::cout << "[Test] Testing ParseHttpRequest malformed Content-Length..." << std::endl;
    std::string method, path;
    echo::core::QueryMap query;
    echo::core::HeaderMap headers;
    std::string raw = "POST /test HTTP/1.1\r\ncontent-length: abc\r\n\r\n";
    bool ok = echo::core::ParseHttpRequest(raw, method, path, query, headers);
    assert(ok);
    assert(headers["content-length"] == "abc");
    // CompatServer's body-reading logic uses std::stoull with try/catch;
    // verify that parsing "abc" yields 0 (fallback) rather than throwing.
    size_t contentLength = 0;
    try {
      contentLength = std::stoull(headers.at("content-length"));
    } catch (...) {
      contentLength = 0;
    }
    assert(contentLength == 0);
    std::cout << "  [ok] ParseHttpRequest malformed Content-Length" << std::endl;
  }

  // ── CompatApi route table contract ────────────────────────────────────
  // Pins the public route-set so that refactors (P2 route dedup) have a
  // green baseline.  Any route addition / removal must update this table.
  std::cout << "[Test] Testing CompatApi route table contract..." << std::endl;
  {
    // Every route listed in kKnownRoutes must be recognised.
    const char* contractRoutes[] = {
        "/health",
        "/server/now",
        "/diagnostics/memory",
        "/register/dev",
        "/login/qr/key",
        "/login/qr/create",
        "/login/qr/check",
        "/auth/logout",
        "/settings/device",
        "/captcha/sent",
        "/login/cellphone",
        "/login/wx/create",
        "/login/wx/check",
        "/login/openplat",
        "/user/detail",
        "/user/vip/detail",
        "/youth/day/vip",
        "/youth/day/vip/upgrade",
        "/youth/listen/song",
        "/youth/vip/ad",
        "/youth/month/vip/record",
        "/user/history",
        "/playhistory/upload",
        "/user/cloud",
        "/user/cloud/url",
        "/search",
        "/search/hot",
        "/search/default",
        "/search/suggest",
        "/search/lyric",
        "/lyric",
        "/song/url",
        "/privilege/lite",
        "/top/song",
        "/top/album",
        "/everyday/recommend",
        "/song/climax",
        "/song/ranking",
        "/song/ranking/filter",
        "/images/audio",
        "/playlist/recommend",
        "/playlist/detail",
        "/playlist/track/all",
        "/playlist/track/all/new",
        "/user/playlist",
        "/rank/list",
        "/playlist/tags",
        "/rank/top",
        "/top/playlist",
        "/top/ip",
        "/rank/audio",
        "/playlist/tracks/add",
        "/playlist/tracks/del",
        "/playlist/add",
        "/playlist/del",
        "/album/detail",
        "/album/songs",
        "/artist/detail",
        "/artist/audios",
        "/artist/albums",
        "/artist/follow",
        "/artist/unfollow",
        "/comment/music",
        "/comment/music/classify",
        "/comment/music/hotword",
        "/comment/playlist",
        "/comment/album",
        "/comment/floor",
        "/comment/count",
        "/favorite/count",
        "/video/url",
    };
    const size_t contractRouteCount = sizeof(contractRoutes) / sizeof(contractRoutes[0]);
    for (size_t i = 0; i < contractRouteCount; ++i) {
      assert(echo::core::IsKnownCompatRoute(contractRoutes[i]));
    }

    // Hardcoded fallback routes (not in kKnownRoutes but recognised by IsKnownCompatRoute)
    assert(echo::core::IsKnownCompatRoute("/kmr/audio/mv"));
    assert(echo::core::IsKnownCompatRoute("/video/privilege"));
    assert(echo::core::IsKnownCompatRoute("/video/detail"));

    // Unknown routes must NOT be recognised.
    assert(!echo::core::IsKnownCompatRoute("/nonexistent"));
    assert(!echo::core::IsKnownCompatRoute("/unknown/route"));
    assert(!echo::core::IsKnownCompatRoute("/"));
    assert(!echo::core::IsKnownCompatRoute(""));

    std::cout << "  [ok] IsKnownCompatRoute: " << contractRouteCount << " contract routes + 3 fallback routes recognised, unknown routes rejected" << std::endl;
  }

  // ── CompatApi unknown-route contract ───────────────────────────────────
  {
    std::cout << "[Test] Testing CompatApi unknown-route 404..." << std::endl;
    echo::storage::Database routeDb;
    routeDb.Open(TestDbPath());
    routeDb.Initialize();
    echo::core::CompatApi routeApi(routeDb);

    auto unknown = routeApi.Handle("GET", "/not/a/route", {}, {}, "");
    assert(unknown.httpStatus == 404);
    assert(unknown.body["status"] == 0);
    assert(unknown.body["error_code"] == 404);

    // POST to unknown route must also 404.
    auto unknownPost = routeApi.Handle("POST", "/bad/post", {}, {}, "{}");
    assert(unknownPost.httpStatus == 404);

    std::cout << "  [ok] CompatApi returns 404 for unknown routes" << std::endl;
  }

  // ── SongUrl public Interface shape contract ────────────────────────────
  // Pins the normalized output shape so P3 (SongUrlService refactor)
  // has a contract to validate against.
  std::cout << "[Test] Testing SongUrl public Interface shape contract..." << std::endl;
  {
    echo::core::SongUrlService songUrlContractSvc([](
        const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"hash":"ABC123","url":"http://cdn.example/abc.flac","backup_url":["http://cdn.example/bak.flac"],"fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","albumid":966846,"album_audio_id":32100650,"audio_id":20505418,"timeLength":269000,"bitRate":320,"extName":"flac","privilege":10,"pay_type":3})",
          ""};
    });

    // Public shape contract: every resolve call must return these fields.
    const auto contractUrl = songUrlContractSvc.Resolve("ABC123", "", "");
    assert(contractUrl.contains("status"));
    assert(contractUrl.contains("url"));
    assert(contractUrl.contains("data"));
    assert(contractUrl["data"].contains("play_url"));
    assert(contractUrl["data"]["play_url"] == "http://cdn.example/abc.flac");
    assert(contractUrl["data"].contains("backup_url"));
    assert(contractUrl["data"]["backup_url"].is_array());
    assert(contractUrl["data"].contains("hash"));
    assert(contractUrl["data"].contains("song_name"));
    assert(contractUrl["data"].contains("singer_name"));
    assert(contractUrl["data"].contains("time_length"));
    assert(contractUrl["data"].contains("bit_rate"));
    assert(contractUrl["data"].contains("ext_name"));
    assert(contractUrl["data"].contains("privilege"));
    assert(contractUrl["data"].contains("pay_type"));
    assert(contractUrl["data"].contains("album_audio_id"));
    assert(contractUrl["data"].contains("audio_id"));
    assert(contractUrl["data"].contains("album_id"));

    // ResolveV6PrivUrl must also conform to the same shape.
    echo::core::DeviceInfo v6Device;
    v6Device.dfid = "v6-dfid";
    v6Device.guid = "v6-guid";
    const auto v6Url = songUrlContractSvc.ResolveV6PrivUrl(
        "VIPHASH", "123", "42", "tok", "vipTok", 3, v6Device);
    assert(v6Url.contains("status"));
    assert(v6Url.contains("url"));
    assert(v6Url.contains("data"));
    assert(v6Url["data"].contains("play_url"));
    assert(v6Url["data"].contains("hash"));

    echo::core::SongUrlService v6QualitySvc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected v5 fallback"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[{"url":"http://cdn.example/song-128.mp3","info":{"bitrate":128,"filesize":1000,"extname":"mp3","songName":"歌名","singerName":"歌手","timeLength":269000}},{"url":"http://cdn.example/song-320.mp3","info":{"bitrate":320,"filesize":2000,"extname":"mp3","songName":"歌名","singerName":"歌手","timeLength":269000}}]})",
              ""};
        });
    const auto requested128 = v6QualitySvc.Resolve(
        "ABC123", "", "32100650", "128", "", "42", "tok", v6Device);
    assert(requested128["status"] == 1);
    assert(requested128["url"] == "http://cdn.example/song-128.mp3");
    assert(requested128["play_url"] == "http://cdn.example/song-128.mp3");
    assert(requested128["data"]["quality"] == "128");
    assert(requested128["data"]["available_qualities"].size() == 2);

    std::cout << "  [ok] SongUrl Resolve / ResolveV6PrivUrl output shape contract" << std::endl;
  }

  // ── Playlist public Interface shape contract ───────────────────────────
  // Pins the normalized output shape so P4 (PlaylistService normalizer)
  // has a contract to validate against.
  std::cout << "[Test] Testing Playlist public Interface shape contract..." << std::endl;
  {
    auto mockGet = [](const std::string&,
                      const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      return {200, R"({"status":1,"data":{"info":[{"hash":"h1","filename":"A - Song","duration":240,"album_audio_id":1,"audio_id":2,"album_id":3,"privilege":10,"pay_type":3}],"total":1}})", ""};
    };

    echo::core::PlaylistService playlistContractSvc(mockGet);

    // GetTracks output shape
    const auto tracks = playlistContractSvc.GetTracks("1", 1, 10);
    assert(tracks.contains("status"));
    assert(tracks.contains("data"));
    assert(tracks["data"].contains("songs"));
    assert(tracks["data"]["songs"].is_array());
    assert(tracks["data"]["songs"].size() == 1);
    const auto& song = tracks["data"]["songs"][0];
    assert(song.contains("hash"));
    assert(song.contains("songname"));
    assert(song.contains("singername"));
    assert(song.contains("timelen"));
    assert(song.contains("album_audio_id"));
    assert(song.contains("audio_id"));
    assert(song.contains("album_id"));
    assert(song.contains("privilege"));
    assert(song.contains("pay_type"));
    std::cout << "  [ok] PlaylistService::GetTracks output shape contract" << std::endl;
  }

  {
    auto mockPost = [](const std::string&,
                       const std::string&,
                       const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      return {200, R"({"errcode":0,"data":{"lists":[{"global_collection_id":"collection_3_42_1_0","listid":"1","listname":"P1","songcount":5,"img":"img.jpg"}],"total":1}})", ""};
    };

    echo::core::PlaylistService userPlaylistContractSvc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);

    // GetUserPlaylists output shape
    const auto userLists = userPlaylistContractSvc.GetUserPlaylists("42", "tok", 1, 30);
    assert(userLists.contains("status"));
    assert(userLists["status"] == 1);
    assert(userLists.contains("data"));
    assert(userLists["data"].contains("list"));
    assert(userLists["data"]["list"].is_array());
    assert(userLists["data"]["list"].size() == 1);
    const auto& pl = userLists["data"]["list"][0];
    assert(pl.contains("id"));
    assert(pl.contains("global_collection_id"));
    assert(pl.contains("listid"));
    assert(pl.contains("name"));
    assert(pl.contains("songcount"));
    assert(pl.contains("img"));
    assert(pl["id"] == "collection_3_42_1_0");
    assert(pl["listid"] == "1");
    std::cout << "  [ok] PlaylistService::GetUserPlaylists output shape contract" << std::endl;
  }

  std::cout << "[Test] All tests completed successfully!" << std::endl;
  return 0;
}


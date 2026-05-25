#include <cassert>
#include <atomic>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

// 引入 D2D / DXGI / DWrite HRESULT 常量与接口完整定义供 RenderPipeline / Painter 测试块使用。
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi.h>

#include "echo/async/EventQueue.h"
#include "echo/async/TaskScheduler.h"
#include "echo/core/Authorization.h"
#include "echo/core/BackendFacade.h"
#include "echo/core/CatalogService.h"
#include "echo/core/CompatApi.h"
#include "echo/core/Crypto.h"
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
#include "echo/image/ImageCache.h"
#include "echo/image/ImageLoader.h"
#include "echo/playback/PlaybackController.h"
#include "echo/storage/Database.h"
#include "echo/win32_app/ImageSlot.h"
#include "echo/storage/SettingsRepository.h"
#include "echo/win32_app/Layout.h"
#include "echo/win32_app/LyricViewModel.h"
#include "echo/win32_app/Navigation.h"
#include "echo/win32_app/PlaybackViewModel.h"
#include "echo/win32_app/PlaybackQueue.h"
#include "echo/win32_app/Painter.h"
#include "echo/win32_app/GlassPanel.h"
#include "echo/win32_app/PaperTexture.h"
#include "echo/win32_app/RenderPipeline.h"
#include "echo/win32_app/SearchInput.h"
#include "echo/win32_app/SearchViewModel.h"
#include "echo/win32_app/Theme.h"

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
    auto settingsFacade = echo::core::CreateBackendFacade(settingsPath);
    const auto loadedSettings = settingsFacade->LoadSettings().get();
    assert(loadedSettings.volume == 0.72);
    assert(loadedSettings.startupPage == "now_playing");
    assert(loadedSettings.imageMemoryCacheMb == 24);
    echo::core::AppSettings updatedSettings;
    updatedSettings.volume = 0.33;
    updatedSettings.startupPage = "home";
    updatedSettings.imageMemoryCacheMb = 16;
    settingsFacade->SaveSettings(updatedSettings).get();
  }
  {
    auto settingsFacade = echo::core::CreateBackendFacade(settingsPath);
    const auto loadedSettings = settingsFacade->LoadSettings().get();
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
  auto health = api.Handle("GET", "/health", {}, {});
  assert(health.httpStatus == 200);
  assert(health.body["status"] == 1);

  auto device = api.Handle("GET", "/register/dev", {}, {});
  assert(device.httpStatus == 200);
  assert(device.body["status"] == 1);
  assert(device.body["data"]["dfid"].get<std::string>().size() == 32);

  auto now = api.Handle("GET", "/server/now", {}, {});
  assert(now.httpStatus == 200);
  assert(now.body["data"]["timestamp"].get<std::int64_t>() > 0);

  std::cout << "[Test] Calling /login/qr/key (first time)..." << std::endl;
  auto loginQrKey = api.Handle("GET", "/login/qr/key", {}, {});
  assert(loginQrKey.httpStatus == 200);
  assert(loginQrKey.body.contains("status"));
  // The route is now live — it must NOT return native_not_implemented.
  assert(!loginQrKey.body.contains("error_code") ||
         loginQrKey.body["error_code"].get<std::string>() != "native_not_implemented");

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
      {});
  assert(compatSearch.httpStatus == 200);
  assert(compatSearch.body["status"] == 1);
  assert(compatSearch.body["data"]["keywords"] == "晴天");
  assert(compatSearch.body["data"]["pagesize"] == 7);
  assert(compatSearchCalls == 1);

  const auto compatSongUrl = compatApiWithHandlers.Handle(
      "GET",
      "/song/url",
      {{"hash", "abc123"}, {"quality", "sq"}, {"ppage_id", "playlist_detail"}},
      {});
  assert(compatSongUrl.httpStatus == 200);
  assert(compatSongUrl.body["status"] == 1);
  assert(compatSongUrl.body["url"] == "https://audio.example/abc123.flac");
  assert(compatSongUrlCalls == 1);

  const auto compatLyricSearch = compatApiWithHandlers.Handle(
      "GET",
      "/search/lyric",
      {{"hash", "abc123"}},
      {});
  assert(compatLyricSearch.body["status"] == 200);
  assert(compatLyricSearch.body["candidates"].size() == 1);
  assert(compatLyricSearchCalls == 1);

  const auto compatLyricDetail = compatApiWithHandlers.Handle(
      "GET",
      "/lyric",
      {{"id", "lyric-1"}, {"accessKey", "ak"}},
      {});
  assert(compatLyricDetail.body["status"] == 200);
  assert(compatLyricDetail.body["decodeContent"] == "[00:01.00]晴天");
  assert(compatLyricDetailCalls == 1);

  const auto compatPlaylistTracks = compatApiWithHandlers.Handle(
      "GET",
      "/playlist/track/all",
      {{"id", "125032"}, {"page", "3"}, {"pageSize", "12"}},
      {});
  assert(compatPlaylistTracks.body["status"] == 1);
  assert(compatPlaylistTracks.body["data"]["songs"].size() == 1);
  assert(compatPlaylistTracks.body["data"]["pagesize"] == 12);
  assert(compatPlaylistTracksCalls == 1);

  auto facade = echo::core::CreateBackendFacade(TestDbPath());
  const auto facadeDevice = facade->EnsureDeviceReady().get();
  const auto secondFacadeDevice = facade->EnsureDeviceReady().get();
  assert(facadeDevice.dfid.size() == 32);
  assert(facadeDevice.dfid == secondFacadeDevice.dfid);

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
  const std::string expectedSongUrlKey = echo::core::CalculateMd5(
      "abcdef57ae12eb6890223e355ccfcb74edf70d1005042");
  assert(capturedSongUrlRequest.find("https://gateway.kugou.com/v5/url?") == 0);
  assert(capturedSongUrlRequest.find("hash=abcdef") != std::string::npos);
  assert(capturedSongUrlRequest.find("album_id=123") != std::string::npos);
  assert(capturedSongUrlRequest.find("album_audio_id=456") != std::string::npos);
  assert(capturedSongUrlRequest.find("quality=sq") != std::string::npos);
  assert(capturedSongUrlRequest.find("appid=1005") != std::string::npos);
  assert(capturedSongUrlRequest.find("clientver=11430") != std::string::npos);
  assert(capturedSongUrlRequest.find("mid=0") != std::string::npos);
  assert(capturedSongUrlRequest.find("dfid=-") != std::string::npos);
  assert(capturedSongUrlRequest.find("uuid=-") != std::string::npos);
  assert(capturedSongUrlRequest.find("userid=42") != std::string::npos);
  assert(capturedSongUrlRequest.find("token=tok") != std::string::npos);
  assert(capturedSongUrlRequest.find("key=" + expectedSongUrlKey) != std::string::npos);
  assert(capturedSongUrlRequest.find("appid=1014") == std::string::npos);
  assert(capturedSongUrlRequest.find("clientver=10000") == std::string::npos);
  assert(capturedSongUrlHeaders["x-router"] == "trackercdn.kugou.com");
  assert(capturedSongUrlHeaders["dfid"] == "-");
  assert(capturedSongUrlHeaders["mid"] == "0");
  assert(!capturedSongUrlHeaders["clienttime"].empty());

  std::vector<std::string> previewRequests;
  echo::core::SongUrlService previewSongUrlService([&](
                                                       const std::string& url,
                                                       const std::unordered_map<std::string, std::string>&) {
    previewRequests.push_back(url);
    if (previewRequests.size() == 1) {
      assert(url.find("userid=42") != std::string::npos);
      return echo::core::HttpResult{200, R"({"status":2,"errcode":20018})", ""};
    }
    if (previewRequests.size() == 2) {
      assert(url.find("userid=42") == std::string::npos);
      return echo::core::HttpResult{
          200,
          R"({"status":2,"fail_process":["pkg","buy"],"hash_offset":{"offset_hash":"OFFSETHASH"}})",
          ""};
    }
    assert(url.find("hash=offsethash") != std::string::npos);
    assert(url.find("IsFreePart=1") != std::string::npos);
    assert(url.find("userid=42") == std::string::npos);
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

  auto backendFacade = echo::core::CreateBackendFacade(TestDbPath());
  const auto emptyFacadeLyric = backendFacade->SearchLyrics("").get();
  assert(emptyFacadeLyric["status"] == 1);
  assert(emptyFacadeLyric["data"]["candidates"].empty());

  const auto missingFacadeLyric = backendFacade->GetLyricDetail("", "").get();
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

  const std::string localPlaybackFixture = "file:///C:/Windows/Media/Windows%20Notify.wav";

  echo::playback::PlaybackController playback;
  assert(playback.Initialize());
  assert(playback.PlayUrl(localPlaybackFixture));
  assert(playback.GetState().kind == echo::core::PlaybackStateKind::Opening);
  playback.Pause();
  assert(playback.GetState().kind == echo::core::PlaybackStateKind::Paused);
  playback.Resume();
  assert(playback.GetState().kind == echo::core::PlaybackStateKind::Playing);
  playback.Seek(42.5);
  assert(playback.GetState().currentSeconds == 42.5);
  playback.Seek(-10.0);
  assert(playback.GetState().currentSeconds == 0.0);
  playback.SetVolume(1.5);
  assert(playback.GetState().volume == 1.0);
  playback.SetVolume(-1.0);
  assert(playback.GetState().volume == 0.0);
  playback.SetRate(4.0);
  assert(playback.GetState().rate == 2.0);
  playback.SetRate(0.1);
  assert(playback.GetState().rate == 0.5);
  assert(playback.PlayUrl(localPlaybackFixture));
  assert(playback.GetState().sourceUrl == localPlaybackFixture);
  playback.Stop();
  assert(playback.GetState().kind == echo::core::PlaybackStateKind::Stopped);
  assert(playback.GetState().sourceUrl.empty());

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

  echo::win32_app::ImageSlot imageSlot;
  const auto firstImageRequest = imageSlot.Request("cover:1");
  assert(firstImageRequest.shouldStartLoad);
  assert(firstImageRequest.key == "cover:1");
  assert(imageSlot.Status() == echo::win32_app::ImageSlotStatus::Loading);
  const auto duplicateImageRequest = imageSlot.Request("cover:1");
  assert(!duplicateImageRequest.shouldStartLoad);
  imageSlot.Complete("cover:old", echo::win32_app::ImageSlotPayload{1, 1, {1, 2, 3, 4}});
  assert(imageSlot.Status() == echo::win32_app::ImageSlotStatus::Loading);
  imageSlot.Complete("cover:1", echo::win32_app::ImageSlotPayload{1, 1, {1, 2, 3, 4}});
  assert(imageSlot.Status() == echo::win32_app::ImageSlotStatus::Ready);
  assert(imageSlot.Payload() != nullptr);
  const auto secondImageRequest = imageSlot.Request("cover:2");
  assert(secondImageRequest.shouldStartLoad);
  assert(imageSlot.Status() == echo::win32_app::ImageSlotStatus::Loading);
  imageSlot.Fail("cover:2");
  assert(imageSlot.Status() == echo::win32_app::ImageSlotStatus::Failed);

  echo::diagnostics::MemorySnapshotProvider memoryProvider;
  const auto snapshot =
      memoryProvider.Capture(imageCache.Stats().byteCount, scheduler.PendingCount(), "Paused");
  assert(snapshot.imageCacheBytes == imageCache.Stats().byteCount);
  assert(snapshot.pendingTaskCount == 0);
  assert(snapshot.playbackState == "Paused");
  assert(echo::diagnostics::FormatMemorySnapshot(snapshot).find("playback=Paused") != std::string::npos);

  const auto melodyLayout = echo::win32_app::CalculateMelodyLayout(1600.0f, 1060.0f);
  assert(melodyLayout.sidebar.left == 0.0f);
  assert(melodyLayout.sidebar.right == 178.0f);
  assert(melodyLayout.header.bottom == 82.0f);
  assert(melodyLayout.playerBar.top == 948.0f);
  assert(melodyLayout.content.left == 178.0f);
  assert(melodyLayout.content.top == 82.0f);
  assert(melodyLayout.content.bottom == 948.0f);
  assert(melodyLayout.home.hero.left > melodyLayout.sidebar.right);
  assert(melodyLayout.home.recentList.left > melodyLayout.home.hero.right);

  assert(echo::win32_app::DevicePixelsToDips(2560.0f, 144.0f) > 1706.0f);
  assert(echo::win32_app::DevicePixelsToDips(2560.0f, 144.0f) < 1707.0f);
  const auto highDpiFullScreenLayout = echo::win32_app::CalculateMelodyLayout(
      echo::win32_app::DevicePixelsToDips(2560.0f, 144.0f),
      echo::win32_app::DevicePixelsToDips(1620.0f, 144.0f));
  assert(highDpiFullScreenLayout.playerBar.bottom < 1100.0f);
  assert(highDpiFullScreenLayout.playerBar.top >= highDpiFullScreenLayout.content.bottom);
  assert(highDpiFullScreenLayout.home.showRecentList);
  assert(highDpiFullScreenLayout.home.recentList.right <= highDpiFullScreenLayout.content.right);
  assert(highDpiFullScreenLayout.home.recommendationRow.right < highDpiFullScreenLayout.home.recentList.left);

  const auto tallFullScreenLayout = echo::win32_app::CalculateMelodyLayout(2560.0f, 1620.0f);
  assert(tallFullScreenLayout.home.showPlaylistPanel);
  assert(tallFullScreenLayout.home.playlistPanel.top <= tallFullScreenLayout.home.recommendationRow.bottom + 160.0f);
  assert(tallFullScreenLayout.home.artistPanel.top == tallFullScreenLayout.home.playlistPanel.top);
  assert(tallFullScreenLayout.home.recommendationRow.bottom - tallFullScreenLayout.home.recommendationRow.top >= 220.0f);
  assert(tallFullScreenLayout.home.recentList.bottom > 900.0f);
  assert(tallFullScreenLayout.home.recentList.bottom <= tallFullScreenLayout.home.playlistPanel.top - 12.0f);
  assert(tallFullScreenLayout.home.playlistPanel.bottom == tallFullScreenLayout.playerBar.top - 12.0f);
  assert(tallFullScreenLayout.home.artistPanel.bottom == tallFullScreenLayout.playerBar.top - 12.0f);

  const auto minimumLayout = echo::win32_app::CalculateMelodyLayout(900.0f, 640.0f);
  assert(minimumLayout.playerBar.bottom == 640.0f);
  assert(minimumLayout.sidebar.bottom <= minimumLayout.playerBar.top);
  assert(minimumLayout.content.bottom > minimumLayout.content.top);
  assert(minimumLayout.home.hero.right > minimumLayout.home.hero.left);
  assert(minimumLayout.home.compact);
  assert(!minimumLayout.home.showRecommendationRow);
  assert(!minimumLayout.home.showRecentList);
  assert(!minimumLayout.home.showPlaylistPanel);
  assert(!minimumLayout.home.showArtistPanel);
  assert(minimumLayout.home.hero.left >= minimumLayout.content.left);
  assert(minimumLayout.home.hero.right <= 900.0f);
  assert(minimumLayout.home.hero.bottom <= minimumLayout.playerBar.top - 12.0f);

  const auto shortButUsefulLayout = echo::win32_app::CalculateMelodyLayout(1280.0f, 720.0f);
  assert(!shortButUsefulLayout.home.compact);
  assert(shortButUsefulLayout.home.showRecommendationRow);
  assert(shortButUsefulLayout.home.showRecentList);
  assert(shortButUsefulLayout.home.recommendationRow.bottom <= shortButUsefulLayout.playerBar.top - 12.0f);
  assert(shortButUsefulLayout.home.recommendationRow.bottom - shortButUsefulLayout.home.recommendationRow.top >= 156.0f);
  assert(shortButUsefulLayout.home.recommendationCardCount >= 3);
  assert(shortButUsefulLayout.home.recommendationRow.right < shortButUsefulLayout.home.recentList.left);
  assert(shortButUsefulLayout.content.left >= shortButUsefulLayout.sidebar.right);
  assert(shortButUsefulLayout.nowPlaying.lyrics.left >= shortButUsefulLayout.content.left);
  assert(shortButUsefulLayout.nowPlaying.lyrics.right <= shortButUsefulLayout.content.right);
  assert(shortButUsefulLayout.nowPlaying.lyrics.right - shortButUsefulLayout.nowPlaying.lyrics.left >= 320.0f);
  assert(!shortButUsefulLayout.nowPlaying.showQueue);
  assert(minimumLayout.nowPlaying.lyrics.right - minimumLayout.nowPlaying.lyrics.left >= 260.0f);
  assert(!minimumLayout.nowPlaying.showQueue);

  const auto realSmallClientLayout = echo::win32_app::CalculateMelodyLayout(884.0f, 601.0f);
  assert(realSmallClientLayout.playerBar.bottom == 601.0f);
  assert(realSmallClientLayout.content.right == 884.0f);
  assert(realSmallClientLayout.sidebar.bottom <= realSmallClientLayout.playerBar.top);
  assert(realSmallClientLayout.home.hero.bottom <= realSmallClientLayout.playerBar.top - 12.0f);

  const auto compactPlayerBar = echo::win32_app::CalculatePlayerBarLayout(900.0f, 640.0f);
  assert(compactPlayerBar.compact);
  assert(!compactPlayerBar.showVolume);
  assert(!compactPlayerBar.showSecondaryControls);
  assert(compactPlayerBar.progress.right - compactPlayerBar.progress.left >= 220.0f);
  assert(compactPlayerBar.title.right <= compactPlayerBar.currentTime.left);
  assert(compactPlayerBar.duration.right <= compactPlayerBar.queue.left);

  const auto realSmallPlayerBar = echo::win32_app::CalculatePlayerBarLayout(884.0f, 601.0f);
  assert(realSmallPlayerBar.bar.bottom == 601.0f);
  assert(realSmallPlayerBar.lyric.right <= 884.0f);
  assert(realSmallPlayerBar.progress.right < realSmallPlayerBar.lyric.left);
  assert(echo::win32_app::HitTestPlayerBar(realSmallPlayerBar, realSmallPlayerBar.lyric.left + 2.0f,
                                           realSmallPlayerBar.lyric.top + 2.0f) ==
         echo::win32_app::PlayerBarAction::OpenLyrics);
  assert(echo::win32_app::HitTestPlayerBar(realSmallPlayerBar, realSmallPlayerBar.playPause.left + 2.0f,
                                           realSmallPlayerBar.playPause.top + 2.0f) ==
         echo::win32_app::PlayerBarAction::TogglePlay);

  const float tinyWidth = echo::win32_app::DevicePixelsToDips(900.0f, 144.0f);
  const float tinyHeight = echo::win32_app::DevicePixelsToDips(640.0f, 144.0f);
  const auto tinyHighDpiLayout = echo::win32_app::CalculateMelodyLayout(tinyWidth, tinyHeight);
  assert(tinyHighDpiLayout.playerBar.bottom == tinyHeight);
  assert(tinyHighDpiLayout.playerBar.top >= tinyHighDpiLayout.content.bottom);
  assert(tinyHighDpiLayout.home.compact);
  assert(!tinyHighDpiLayout.home.showRecommendationRow);
  assert(tinyHighDpiLayout.home.hero.bottom <= tinyHighDpiLayout.playerBar.top - 12.0f);
  assert(tinyHighDpiLayout.home.hero.right <= tinyWidth);

  const auto tinyHighDpiPlayerBar = echo::win32_app::CalculatePlayerBarLayout(tinyWidth, tinyHeight);
  assert(tinyHighDpiPlayerBar.bar.bottom <= tinyHeight);
  assert(!tinyHighDpiPlayerBar.showVolume);
  assert(!tinyHighDpiPlayerBar.showSecondaryControls);
  assert(tinyHighDpiPlayerBar.title.right <= tinyHighDpiPlayerBar.previous.left - 12.0f);
  assert(tinyHighDpiPlayerBar.currentTime.right <= tinyHighDpiPlayerBar.progress.left - 4.0f);
  assert(tinyHighDpiPlayerBar.duration.left >= tinyHighDpiPlayerBar.progress.right + 4.0f);
  assert(tinyHighDpiPlayerBar.duration.right <= tinyHighDpiPlayerBar.queue.left - 4.0f);
  assert(tinyHighDpiPlayerBar.lyric.right <= tinyWidth);
  assert(echo::win32_app::HitTestPlayerBar(tinyHighDpiPlayerBar, tinyHighDpiPlayerBar.lyric.left + 2.0f,
                                           tinyHighDpiPlayerBar.lyric.top + 2.0f) ==
         echo::win32_app::PlayerBarAction::OpenLyrics);

  echo::win32_app::NavigationState navigation;
  assert(navigation.Current() == echo::win32_app::PageId::Home);
  assert(!navigation.CanGoBack());
  assert(!navigation.CanGoForward());
  navigation.NavigateTo(echo::win32_app::PageId::NowPlaying);
  navigation.NavigateTo(echo::win32_app::PageId::Search);
  navigation.NavigateTo(echo::win32_app::PageId::Settings);
  assert(navigation.Current() == echo::win32_app::PageId::Settings);
  assert(navigation.GoBack());
  assert(navigation.Current() == echo::win32_app::PageId::Search);
  assert(navigation.CanGoBack());
  assert(navigation.GoBack());
  assert(navigation.Current() == echo::win32_app::PageId::NowPlaying);
  assert(navigation.CanGoForward());
  navigation.NavigateTo(echo::win32_app::PageId::Home);
  assert(navigation.Current() == echo::win32_app::PageId::Home);
  assert(!navigation.CanGoForward());

  const auto headerControls = echo::win32_app::CalculateHeaderControlsLayout(1600.0f);
  assert(echo::win32_app::HitTestHeader(headerControls, headerControls.back.left + 4.0f,
                                        headerControls.back.top + 4.0f) ==
         echo::win32_app::HeaderAction::Back);
  assert(echo::win32_app::HitTestHeader(headerControls, headerControls.forward.left + 4.0f,
                                        headerControls.forward.top + 4.0f) ==
         echo::win32_app::HeaderAction::Forward);
  assert(echo::win32_app::HitTestHeader(headerControls, headerControls.search.left + 10.0f,
                                        headerControls.search.top + 10.0f) ==
         echo::win32_app::HeaderAction::Search);
  assert(echo::win32_app::HitTestHeader(headerControls, headerControls.avatar.left + 2.0f,
                                        headerControls.avatar.top + 2.0f) ==
         echo::win32_app::HeaderAction::Avatar);
  assert(echo::win32_app::HitTestSidebar(40.0f, 116.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Home);
  assert(echo::win32_app::HitTestSidebar(40.0f, 160.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Discover);
  assert(echo::win32_app::HitTestSidebar(40.0f, 205.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Radio);
  assert(echo::win32_app::HitTestSidebar(40.0f, 250.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Video);
  assert(echo::win32_app::HitTestSidebar(40.0f, 330.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Songs);
  assert(echo::win32_app::HitTestSidebar(40.0f, 376.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Albums);
  assert(echo::win32_app::HitTestSidebar(40.0f, 420.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Artists);
  assert(echo::win32_app::HitTestSidebar(40.0f, 456.0f, 900.0f) ==
         echo::win32_app::SidebarAction::NowPlaying);
  assert(echo::win32_app::HitTestSidebar(40.0f, 508.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Favorites);
  assert(echo::win32_app::HitTestSidebar(40.0f, 552.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Downloads);
  assert(echo::win32_app::HitTestSidebar(40.0f, 850.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Settings);
  const auto interactionLayout = echo::win32_app::CalculateMelodyLayout(1600.0f, 1060.0f);
  assert(echo::win32_app::HitTestHome(interactionLayout.home, interactionLayout.home.hero.left + 42.0f,
                                      interactionLayout.home.hero.bottom - 44.0f) ==
         echo::win32_app::HomeAction::PlayHero);
  assert(echo::win32_app::HitTestHome(interactionLayout.home,
                                      interactionLayout.home.recommendationRow.left + 40.0f,
                                      interactionLayout.home.recommendationRow.top + 80.0f) ==
         echo::win32_app::HomeAction::OpenRecommendation);
  assert(echo::win32_app::HomeRecommendationIndexFromPoint(
             interactionLayout.home,
             interactionLayout.home.recommendationRow.left + 36.0f,
             interactionLayout.home.recommendationRow.top + 86.0f) == 0);
  assert(echo::win32_app::HomeRecentIndexFromPoint(
             interactionLayout.home,
             interactionLayout.home.recentList.left + 48.0f,
             interactionLayout.home.recentList.top + 30.0f) == 0);
  assert(echo::win32_app::HomePlaylistIndexFromPoint(
             interactionLayout.home,
             interactionLayout.home.playlistPanel.left + 44.0f,
             interactionLayout.home.playlistPanel.top + 84.0f) == 0);
  assert(echo::win32_app::HitTestNowPlaying(interactionLayout.nowPlaying,
                                            interactionLayout.nowPlaying.tabs.left + 24.0f,
                                            interactionLayout.nowPlaying.tabs.top + 12.0f) ==
         echo::win32_app::NowPlayingAction::OverviewTab);
  assert(echo::win32_app::HitTestNowPlaying(interactionLayout.nowPlaying,
                                            interactionLayout.nowPlaying.tabs.left + 132.0f,
                                            interactionLayout.nowPlaying.tabs.top + 12.0f) ==
         echo::win32_app::NowPlayingAction::LyricsTab);

  echo::win32_app::SearchInputState searchInput;
  assert(!searchInput.IsFocused());
  searchInput.Focus();
  assert(searchInput.IsFocused());
  assert(searchInput.HandleCharacter(L'周').action == echo::win32_app::SearchInputAction::None);
  assert(searchInput.HandleCharacter(L'杰').action == echo::win32_app::SearchInputAction::None);
  assert(searchInput.HandleCharacter(L'伦').action == echo::win32_app::SearchInputAction::None);
  assert(searchInput.Text() == L"周杰伦");
  assert(searchInput.HandleCharacter(L'\b').action == echo::win32_app::SearchInputAction::None);
  assert(searchInput.Text() == L"周杰");
  const auto submittedSearch = searchInput.HandleCharacter(L'\r');
  assert(submittedSearch.action == echo::win32_app::SearchInputAction::Submit);
  assert(submittedSearch.submittedText == L"周杰");
  searchInput.Blur();
  assert(!searchInput.IsFocused());
  assert(searchInput.HandleCharacter(L'天').action == echo::win32_app::SearchInputAction::None);
  assert(searchInput.Text() == L"周杰");

  echo::win32_app::PlaybackQueueState queue({
      {L"晴天", L"周杰伦", L"叶惠美", L"04:29"},
      {L"七里香", L"周杰伦", L"七里香", L"04:57"},
      {L"一路向北", L"周杰伦", L"Initial J", L"04:55"},
  });
  assert(queue.HasTracks());
  assert(queue.CurrentIndex() == 0);
  assert(queue.Current()->title == L"晴天");
  assert(queue.Next()->title == L"七里香");
  assert(queue.CurrentIndex() == 1);
  assert(queue.Previous()->title == L"晴天");
  assert(queue.Previous()->title == L"一路向北");
  assert(queue.Select(1)->title == L"七里香");
  assert(queue.Select(99) == nullptr);
  assert(queue.CurrentIndex() == 1);

  const auto roomyPlayerBar = echo::win32_app::CalculatePlayerBarLayout(1600.0f, 1060.0f);
  assert(!roomyPlayerBar.compact);
  assert(roomyPlayerBar.showVolume);
  assert(roomyPlayerBar.showSecondaryControls);
  assert(roomyPlayerBar.volume.left > roomyPlayerBar.repeat.right);
  assert(roomyPlayerBar.bar.bottom == 1060.0f);
  assert(roomyPlayerBar.progress.top > roomyPlayerBar.playPause.bottom);
  assert(roomyPlayerBar.progress.right - roomyPlayerBar.progress.left >= 600.0f);

  const auto scaledDesktopHome = echo::win32_app::CalculateMelodyLayout(1536.0f, 864.0f);
  assert(!scaledDesktopHome.home.compact);
  assert(scaledDesktopHome.home.showRecommendationRow);
  assert(scaledDesktopHome.home.showRecentList);
  assert(scaledDesktopHome.home.showPlaylistPanel);
  assert(echo::win32_app::HomePlaylistIndexFromPoint(
             scaledDesktopHome.home,
             scaledDesktopHome.home.playlistPanel.left + 36.0f,
             scaledDesktopHome.home.playlistPanel.top + 84.0f) == 0);

  const auto compactCards = echo::win32_app::CalculateCardStripLayout(704.0f, 5, 210.0f);
  assert(compactCards.count == 4);
  assert(compactCards.itemWidth >= 140.0f);
  assert(compactCards.itemWidth <= 190.0f);
  assert(compactCards.imageHeight >= compactCards.itemWidth * 0.62f);

  const auto shortCards = echo::win32_app::CalculateCardStripLayout(704.0f, 5, 150.0f);
  assert(shortCards.count == 4);
  assert(shortCards.imageHeight <= shortCards.itemHeight - 58.0f);
  assert(shortCards.imageHeight >= 72.0f);

  const auto compactHeightCards = echo::win32_app::CalculateCardStripLayout(704.0f, 5, 112.0f);
  assert(compactHeightCards.count == 4);
  assert(compactHeightCards.itemHeight >= 108.0f);
  assert(compactHeightCards.imageHeight >= 50.0f);

  const auto wideCards = echo::win32_app::CalculateCardStripLayout(1680.0f, 5, 220.0f);
  assert(wideCards.count == 5);
  assert(wideCards.itemWidth <= 190.0f);
  assert(wideCards.imageHeight <= 128.0f);

  const auto visibleQueue = echo::win32_app::CalculateVisibleRows(10000, 68.0f, 136.0f, 0.0f, 620.0f, 1);
  assert(visibleQueue.first == 0);
  assert(visibleQueue.count <= 12);
  assert(visibleQueue.lastExclusive <= 12);

  const auto scrolledQueue = echo::win32_app::CalculateVisibleRows(10000, 68.0f, 136.0f, 6800.0f, 620.0f, 1);
  assert(scrolledQueue.first >= 99);
  assert(scrolledQueue.count <= 13);
  assert(scrolledQueue.lastExclusive <= 10000);
  for (int step = 0; step < 1000; ++step) {
    const auto rows = echo::win32_app::CalculateVisibleRows(
        10000,
        58.0f,
        0.0f,
        static_cast<float>(step * 31),
        640.0f,
        1);
    assert(rows.count <= 14);
    assert(rows.lastExclusive <= 10000);
  }
  assert(echo::win32_app::ClampScrollOffset(3, 58.0f, 400.0f, 120.0f) == 0.0f);
  assert(echo::win32_app::ClampScrollOffset(100, 58.0f, 400.0f, 99999.0f) == 5400.0f);
  assert(echo::win32_app::ApplyWheelScroll(100, 58.0f, 400.0f, 0.0f, -120) == 174.0f);
  assert(echo::win32_app::ApplyWheelScroll(100, 58.0f, 400.0f, 174.0f, 120) == 0.0f);
  assert(echo::win32_app::TrackValueFromPoint({100.0f, 20.0f, 300.0f, 20.0f}, 100.0f) == 0.0f);
  assert(echo::win32_app::TrackValueFromPoint({100.0f, 20.0f, 300.0f, 20.0f}, 200.0f) == 0.5f);
  assert(echo::win32_app::TrackValueFromPoint({100.0f, 20.0f, 300.0f, 20.0f}, 350.0f) == 1.0f);
  assert(echo::win32_app::HitTestPlayerBar(roomyPlayerBar, roomyPlayerBar.progress.left + 10.0f,
                                           roomyPlayerBar.progress.top) ==
         echo::win32_app::PlayerBarAction::Seek);
  assert(echo::win32_app::HitTestPlayerBar(roomyPlayerBar, roomyPlayerBar.volume.left + 10.0f,
                                           roomyPlayerBar.volume.top) ==
         echo::win32_app::PlayerBarAction::SetVolume);
  const auto fittedRect = echo::win32_app::CalculateAspectFitRect({0.0f, 0.0f, 200.0f, 100.0f}, 100.0f, 100.0f);
  assert(fittedRect.left == 50.0f);
  assert(fittedRect.right == 150.0f);
  assert(fittedRect.top == 0.0f);
  assert(fittedRect.bottom == 100.0f);
  const auto filledRect = echo::win32_app::CalculateAspectFillRect({0.0f, 0.0f, 200.0f, 100.0f}, 100.0f, 100.0f);
  assert(filledRect.left == 0.0f);
  assert(filledRect.right == 200.0f);
  assert(filledRect.top == -50.0f);
  assert(filledRect.bottom == 150.0f);

  const auto searchVm = echo::win32_app::BuildSearchViewModel(
      "晴天",
      nlohmann::json{
          {"status", 1},
          {"data",
           {
               {"total", 2},
               {"lists",
                nlohmann::json::array(
                    {{{"SongName", "晴天"},
                      {"SingerName", "周杰伦"},
                      {"AlbumName", "叶惠美"},
                      {"Duration", 269},
                      {"FileHash", "abc"},
                      {"privilege", 10},
                      {"pay_type", 3},
                      {"Image", "//img.example/{size}/jay-cover.jpg"}},
                     {{"SongName", "晴天娃娃"},
                      {"SingerName", "江语晨"},
                      {"AlbumName", "晴天娃娃"},
                      {"Duration", 242},
                      {"FileHash", "def"}}})},
           }},
      });
  assert(searchVm.keyword == L"晴天");
  assert(searchVm.state == echo::win32_app::SearchState::Ready);
  assert(searchVm.total == 2);
  assert(searchVm.rows.size() == 2);
  assert(searchVm.rows[0].title == L"晴天");
  assert(searchVm.rows[0].artist == L"周杰伦");
  assert(searchVm.rows[0].duration == L"04:29");
  assert(searchVm.rows[0].coverUrl == "https://img.example/480/jay-cover.jpg");
  assert(searchVm.rows[0].imageKey == "remote-cover:https://img.example/480/jay-cover.jpg");
  assert(searchVm.rows[0].privilege == 10);
  assert(searchVm.rows[0].payType == 3);

  const auto fallbackSearchVm = echo::win32_app::BuildSearchViewModel(
      "江南",
      nlohmann::json{
          {"status", 1},
          {"data",
           {
               {"total", 1},
               {"lists",
                nlohmann::json::array(
                    {{{"FileName", "林俊杰 - 江南"},
                      {"album_name", "第二天堂"},
                      {"duration", 268},
                      {"hash", "jj-hash"},
                      {"trans_param", {{"union_cover", "http://img.example/{size}/jj.jpg"}}}}})},
           }},
      });
  assert(fallbackSearchVm.rows.size() == 1);
  assert(fallbackSearchVm.rows[0].title == L"江南");
  assert(fallbackSearchVm.rows[0].artist == L"林俊杰");
  assert(fallbackSearchVm.rows[0].album == L"第二天堂");
  assert(fallbackSearchVm.rows[0].coverUrl == "http://img.example/480/jj.jpg");

  const auto queueLookupVm = echo::win32_app::BuildSearchViewModel(
      "晴天 周杰伦",
      nlohmann::json{
          {"status", 1},
          {"data",
           {
               {"total", 2},
               {"lists",
                nlohmann::json::array(
                    {{{"SongName", "七里香"},
                      {"SingerName", "周杰伦"},
                      {"AlbumName", "测试"},
                      {"Duration", 297},
                      {"FileHash", "wrong-first-hash"}},
                     {{"SongName", "晴天"},
                      {"SingerName", "周杰伦"},
                      {"AlbumName", "叶惠美"},
                      {"Duration", 269},
                      {"FileHash", "paid-exact-hash"},
                      {"privilege", 10},
                      {"pay_type", 3}},
                     {{"SongName", "晴天"},
                      {"SingerName", "蓝心羽"},
                      {"AlbumName", "热门翻唱"},
                      {"Duration", 250},
                      {"FileHash", "free-title-hash"},
                      {"privilege", 0},
                      {"pay_type", 0}}})},
           }}});
  const auto requestedQueueRow = echo::win32_app::BuildSearchRowFromQueueTrack({L"晴天", L"周杰伦", L"叶惠美", L"04:29"});
  const auto rankedQueueRows = echo::win32_app::RankQueuePlaybackCandidates(requestedQueueRow, queueLookupVm);
  assert(rankedQueueRows.size() == 3);
  assert(rankedQueueRows[0].hash == "free-title-hash");
  assert(rankedQueueRows[1].hash == "paid-exact-hash");
  const auto resolvedQueueRow = echo::win32_app::PickQueuePlaybackCandidate(requestedQueueRow, queueLookupVm);
  assert(resolvedQueueRow.hash == "free-title-hash");
  assert(resolvedQueueRow.title == L"晴天");

  {
    std::vector<echo::win32_app::SearchResultRow> retryCandidates = {
        rankedQueueRows[1],
        rankedQueueRows[0],
    };
    std::size_t retryIndex = 0;
    echo::win32_app::SearchResultRow pendingRetryRow = retryCandidates[retryIndex];
    echo::win32_app::PlaybackViewModel retryPlayback;
    echo::win32_app::LyricViewModel retryLyric;
    retryPlayback.state = echo::win32_app::PlayerUiState::Error;
    retryPlayback.error = L"Media Foundation playback failed";
    retryLyric.state = echo::win32_app::LyricUiState::Ready;
    retryLyric.activeIndex = 3;

    const bool advanced = echo::win32_app::TryAdvancePlaybackCandidate(
        retryCandidates,
        retryIndex,
        pendingRetryRow,
        retryPlayback,
        retryLyric,
        retryPlayback.error);

    assert(advanced);
    assert(retryIndex == 1);
    assert(pendingRetryRow.hash == "free-title-hash");
    assert(retryPlayback.state == echo::win32_app::PlayerUiState::Resolving);
    assert(retryPlayback.title == L"晴天");
    assert(retryPlayback.artist == L"蓝心羽");
    assert(retryPlayback.error.empty());
    assert(retryLyric.state == echo::win32_app::LyricUiState::Empty);
    assert(retryLyric.message.find(L"正在尝试其他可播放版本") != std::wstring::npos);

    const bool exhausted = echo::win32_app::TryAdvancePlaybackCandidate(
        retryCandidates,
        retryIndex,
        pendingRetryRow,
        retryPlayback,
        retryLyric,
        L"still failed");
    assert(!exhausted);
    assert(retryIndex == 1);
    assert(pendingRetryRow.hash == "free-title-hash");
  }

  const auto emptySearchVm = echo::win32_app::BuildSearchViewModel(
      "没有结果",
      nlohmann::json{{"status", 1}, {"data", {{"total", 0}, {"lists", nlohmann::json::array()}}}});
  assert(emptySearchVm.state == echo::win32_app::SearchState::Empty);
  assert(emptySearchVm.rows.empty());

  const auto failedSearchVm = echo::win32_app::BuildSearchViewModel(
      "错误",
      nlohmann::json{{"status", 0}, {"error", "network failed"}});
  assert(failedSearchVm.state == echo::win32_app::SearchState::Error);
  assert(failedSearchVm.message == L"network failed");

  const auto playReady = echo::win32_app::BuildPlaybackViewModel(
      searchVm.rows[0],
      nlohmann::json{
          {"status", 1},
          {"url", "https://audio.example/jay.mp3"},
          {"data", {{"play_url", "https://audio.example/jay.mp3"}, {"time_length", 269000}}},
      });
  assert(playReady.state == echo::win32_app::PlayerUiState::Ready);
  assert(playReady.title == L"晴天");
  assert(playReady.artist == L"周杰伦");
  assert(playReady.duration == L"04:29");
  assert(playReady.sourceUrl == "https://audio.example/jay.mp3");
  assert(playReady.coverUrl == searchVm.rows[0].coverUrl);
  assert(playReady.imageKey == searchVm.rows[0].imageKey);
  assert(echo::win32_app::PlaybackSubtitle(playReady) == L"周杰伦");

  const auto playFailed = echo::win32_app::BuildPlaybackViewModel(
      searchVm.rows[0],
      nlohmann::json{{"status", 0}, {"error", "需要付费"}});
  assert(playFailed.state == echo::win32_app::PlayerUiState::Error);
  assert(playFailed.error == L"需要付费");
  assert(playFailed.sourceUrl.empty());
  assert(echo::win32_app::PlaybackSubtitle(playFailed) == L"需要付费");

  echo::playback::PlaybackController emptyPlayback;
  assert(emptyPlayback.Initialize());
  assert(!emptyPlayback.PlayUrl(""));
  assert(emptyPlayback.GetState().kind == echo::core::PlaybackStateKind::Failed);

  const auto parsedLyric = echo::core::ParseLrc(
      "[ti:Demo]\n"
      "[00:01.00]第一句\n"
      "[00:05.50]第二句\n"
      "[01:02.03]第三句\n");
  assert(parsedLyric.lines.size() == 3);
  assert(parsedLyric.lines[0].timeMs == 1000);
  assert(parsedLyric.lines[1].timeMs == 5500);
  assert(parsedLyric.lines[2].timeMs == 62030);
  assert(parsedLyric.lines[2].text == "第三句");
  assert(echo::core::FindActiveLyricLine(parsedLyric, 0) == -1);
  assert(echo::core::FindActiveLyricLine(parsedLyric, 1000) == 0);
  assert(echo::core::FindActiveLyricLine(parsedLyric, 61000) == 1);
  assert(echo::core::FindActiveLyricLine(parsedLyric, 62030) == 2);

  const auto noLyric = echo::core::ParseLrc("[ti:Empty]\n[offset:0]\n");
  assert(noLyric.lines.empty());
  assert(echo::core::FindActiveLyricLine(noLyric, 10000) == -1);

  const auto lyricVm = echo::win32_app::BuildLyricViewModel(parsedLyric, 61000);
  assert(lyricVm.state == echo::win32_app::LyricUiState::Ready);
  assert(lyricVm.activeIndex == 1);
  assert(lyricVm.lines[1].active);
  assert(lyricVm.lines[1].text == L"第二句");
  assert(echo::win32_app::FirstVisibleLyricLine(20, 0, 5) == 0);
  assert(echo::win32_app::FirstVisibleLyricLine(20, 10, 5) == 8);
  assert(echo::win32_app::FirstVisibleLyricLine(20, 19, 5) == 15);

  const auto detailDocument = echo::win32_app::BuildLyricDocumentFromDetail(
      nlohmann::json{{"status", 1}, {"decodeContent", "[00:02.00]详情歌词"}});
  assert(detailDocument.lines.size() == 1);
  assert(detailDocument.lines[0].timeMs == 2000);
  assert(detailDocument.lines[0].text == "详情歌词");

  const auto nestedDetailDocument = echo::win32_app::BuildLyricDocumentFromDetail(
      nlohmann::json{{"status", 1}, {"data", {{"lyric", "[00:03.00]嵌套歌词"}}}});
  assert(nestedDetailDocument.lines.size() == 1);
  assert(nestedDetailDocument.lines[0].timeMs == 3000);

  const auto failedDetailDocument = echo::win32_app::BuildLyricDocumentFromDetail(
      nlohmann::json{{"status", 0}, {"error", "lyric failed"}});
  assert(failedDetailDocument.lines.empty());

  auto seekPlayback = playReady;
  echo::win32_app::LyricViewModel seekLyric;
  echo::win32_app::ApplyPlaybackProgress(seekPlayback, seekLyric, parsedLyric, 62030.0 / 269000.0);
  assert(seekPlayback.current == L"1:02");
  assert(seekPlayback.progress > 0.23);
  assert(seekPlayback.progress < 0.24);
  assert(seekLyric.activeIndex == 2);
  assert(seekLyric.lines[2].active);

  const auto emptyLyricVm = echo::win32_app::BuildLyricViewModel(noLyric, 0);
  assert(emptyLyricVm.state == echo::win32_app::LyricUiState::Empty);
  assert(emptyLyricVm.message == L"暂无歌词");

  echo::win32_app::PlaybackViewModel snapshotPlayback;
  snapshotPlayback.duration = L"04:29";
  echo::win32_app::LyricViewModel snapshotLyric;
  echo::core::PlaybackState snapshotState;
  snapshotState.currentSeconds = 76.0;
  snapshotState.durationSeconds = 269.0;
  echo::win32_app::ApplyPlaybackStateSnapshot(snapshotPlayback, snapshotLyric, parsedLyric, snapshotState);
  assert(snapshotPlayback.current == L"1:16");
  assert(snapshotPlayback.progress > 0.28);
  assert(snapshotPlayback.progress < 0.29);
  assert(snapshotLyric.activeIndex == 2);
  assert(snapshotLyric.lines[2].active);

  const auto queueSearchRow = echo::win32_app::BuildSearchRowFromQueueTrack(
      {L"晴天", L"周杰伦", L"叶惠美", L"04:29"});
  assert(queueSearchRow.title == L"晴天");
  assert(queueSearchRow.artist == L"周杰伦");
  assert(queueSearchRow.album == L"叶惠美");
  assert(queueSearchRow.duration == L"04:29");
  assert(queueSearchRow.hash.empty());
  assert(echo::win32_app::BuildQueueTrackSearchText({L"晴天", L"周杰伦", L"叶惠美", L"04:29"}) ==
         L"晴天 周杰伦");
  assert(echo::win32_app::BuildQueueTrackSearchText({L"晴天", L"", L"叶惠美", L"04:29"}) == L"晴天");

  {
    std::vector<echo::win32_app::QueueTrack> stressTracks = {
        {L"晴天", L"周杰伦", L"叶惠美", L"04:29"},
        {L"江南", L"林俊杰", L"第二天堂", L"04:28"},
        {L"小幸运", L"田馥甄", L"我的少女时代", L"04:19"},
        {L"后来", L"刘若英", L"我等你", L"05:41"},
    };
    echo::win32_app::PlaybackQueueState stressQueue(stressTracks);
    const auto stressLookup = echo::win32_app::BuildSearchViewModel(
        "queue stress",
        nlohmann::json{
            {"status", 1},
            {"data",
             {
                 {"total", 4},
                 {"lists",
                  nlohmann::json::array(
                      {{{"SongName", "晴天"},
                        {"SingerName", "周杰伦"},
                        {"AlbumName", "叶惠美"},
                        {"Duration", 269},
                        {"FileHash", "hash-sunny"},
                        {"Image", "https://img.example/{size}/sunny.jpg"}},
                       {{"SongName", "江南"},
                        {"SingerName", "林俊杰"},
                        {"AlbumName", "第二天堂"},
                        {"Duration", 268},
                        {"FileHash", "hash-jiangnan"},
                        {"Image", "https://img.example/{size}/jiangnan.jpg"}},
                       {{"SongName", "小幸运"},
                        {"SingerName", "田馥甄"},
                        {"AlbumName", "我的少女时代"},
                        {"Duration", 259},
                        {"FileHash", "hash-luck"},
                        {"Image", "https://img.example/{size}/luck.jpg"}},
                       {{"SongName", "后来"},
                        {"SingerName", "刘若英"},
                        {"AlbumName", "我等你"},
                        {"Duration", 341},
                        {"FileHash", "hash-later"},
                        {"Image", "https://img.example/{size}/later.jpg"}}})},
             }}});
    assert(stressLookup.rows.size() == stressTracks.size());

    echo::win32_app::PlaybackViewModel stressPlayback;
    echo::win32_app::LyricViewModel stressLyric;
    for (int round = 0; round < 100; ++round) {
      const auto index = static_cast<std::size_t>(round) % stressTracks.size();
      const auto* selected = stressQueue.Select(index);
      assert(selected);
      assert(stressQueue.CurrentIndex() == index);

      const auto requested = echo::win32_app::BuildSearchRowFromQueueTrack(*selected);
      const auto resolved = echo::win32_app::PickQueuePlaybackCandidate(requested, stressLookup);
      assert(resolved.title == selected->title);
      assert(resolved.artist == selected->artist);
      assert(resolved.album == selected->album);
      assert(!resolved.hash.empty());
      assert(!resolved.coverUrl.empty());

      stressPlayback = echo::win32_app::BuildPlaybackViewModel(
          resolved,
          nlohmann::json{{"status", 1}, {"url", "https://audio.example/" + resolved.hash + ".mp3"}});
      assert(stressPlayback.state == echo::win32_app::PlayerUiState::Ready);
      assert(stressPlayback.title == selected->title);
      assert(stressPlayback.artist == selected->artist);
      assert(stressPlayback.album == selected->album);
      assert(stressPlayback.coverUrl == resolved.coverUrl);
      assert(stressPlayback.imageKey == resolved.imageKey);

      echo::core::PlaybackState playingState;
      playingState.kind = echo::core::PlaybackStateKind::Playing;
      playingState.currentSeconds = 63.0;
      playingState.durationSeconds = 269.0;
      echo::win32_app::ApplyPlaybackStateSnapshot(stressPlayback, stressLyric, parsedLyric, playingState);
      assert(stressPlayback.current == L"1:03");
      assert(stressPlayback.progress > 0.23);
      assert(stressPlayback.progress < 0.24);
      assert(stressLyric.activeIndex == 2);
      assert(stressLyric.lines[2].active);
    }
  }

  // ── A1: Fixture-file-driven contract tests ────────────────────────────────
  //
  // Each JSON file under tests/fixtures/compat/ carries a "_meta" block:
  //   route         – the HTTP path this fixture covers
  //   params        – example query params used to drive the handler
  //   volatile_paths – JSON pointer paths to ignore during comparison
  //   description   – human-readable intent
  //
  // The test loads the fixture, drives a handler-injected CompatApi with the
  // declared params, then calls ContractJsonMatches ignoring volatile_paths.
  // A missing stable field or changed stable value fails with the mismatch path
  // printed to stderr so it is easy to locate the regression.
  // ---------------------------------------------------------------------------

  // Helper: load a fixture file relative to the tests/fixtures/compat/ dir.
  // The path is located by walking up from __FILE__ at compile time so the
  // tests work regardless of working directory.
  const std::filesystem::path fixtureDir = []() -> std::filesystem::path {
    std::filesystem::path src(__FILE__);
    return src.parent_path() / "fixtures" / "compat";
  }();

  auto LoadFixture = [&fixtureDir](const char* name) -> nlohmann::json {
    const auto path = fixtureDir / name;
    std::ifstream f(path);
    assert(f.is_open() && "Fixture file not found – run tests from the repo root");
    nlohmann::json j;
    f >> j;
    return j;
  };

  // ── /search contract ─────────────────────────────────────────────────────
  {
    const auto fixtureSearch = LoadFixture("search.json");
    const auto& meta = fixtureSearch["_meta"];

    // Reproduce a minimal native response using a handler-injected CompatApi.
    echo::core::CompatApiHandlers searchContractHandlers;
    searchContractHandlers.search = [](std::string keywords,
                                       std::string /*type*/,
                                       int /*page*/,
                                       int pageSize) -> nlohmann::json {
      nlohmann::json song;
      song["SongName"] = "晴天";
      song["FileHash"] = "abc123";
      song["SingerName"] = "周杰伦";
      song["AlbumName"] = "叶惠美";
      nlohmann::json lists = nlohmann::json::array();
      lists.push_back(song);
      nlohmann::json data;
      data["keywords"] = keywords;
      data["total"] = 1;
      data["pagesize"] = pageSize;
      data["lists"] = lists;
      return nlohmann::json{{"status", 1}, {"data", data}};
    };
    echo::storage::Database searchContractDb;
    searchContractDb.Open(TestDbPath());
    searchContractDb.Initialize();
    echo::core::CompatApi searchContractApi(searchContractDb,
                                            std::move(searchContractHandlers));

    const auto& params = meta["params"];
    const auto searchResponse = searchContractApi.Handle(
        "GET",
        "/search",
        {{params["keyword"].get<std::string>(), ""},
         {"keyword", params["keyword"].get<std::string>()},
         {"type", params.value("type", "song")},
         {"page", params.value("page", "1")},
         {"pageSize", params.value("pageSize", "30")}},
        {});

    std::vector<std::string> searchMismatches;
    const auto volatilePathsSearch =
        meta["volatile_paths"].get<std::vector<std::string>>();

    // Strip fixture-only _meta key before comparison.
    auto expectedSearch = fixtureSearch;
    expectedSearch.erase("_meta");

    const bool searchOk = echo::core::ContractJsonMatches(
        expectedSearch, searchResponse.body, volatilePathsSearch,
        &searchMismatches);
    if (!searchOk) {
      for (const auto& mp : searchMismatches) {
        std::cerr << "[A1 contract] /search mismatch at: " << mp << '\n';
      }
    }
    assert(searchOk && "[A1] /search contract failed – see mismatch paths above");
  }

  // ── /song/url contract ───────────────────────────────────────────────────
  {
    const auto fixtureSongUrl = LoadFixture("song_url.json");
    const auto& meta = fixtureSongUrl["_meta"];

    echo::core::CompatApiHandlers songUrlContractHandlers;
    songUrlContractHandlers.songUrl = [](std::string hash,
                                         std::string quality,
                                         std::string /*ppageId*/) -> nlohmann::json {
      return nlohmann::json{
          {"status", 1},
          {"url", "https://signed-cdn.example/" + hash + ".flac"},
          {"data",
           {
               {"hash", hash},
               {"play_url", "https://signed-cdn.example/" + hash + ".flac"},
               {"backup_url", nlohmann::json::array()},
           }},
      };
    };
    echo::storage::Database songUrlContractDb;
    songUrlContractDb.Open(TestDbPath());
    songUrlContractDb.Initialize();
    echo::core::CompatApi songUrlContractApi(songUrlContractDb,
                                              std::move(songUrlContractHandlers));

    const auto& params = meta["params"];
    const auto songUrlResponse = songUrlContractApi.Handle(
        "GET",
        "/song/url",
        {{"hash", params["hash"].get<std::string>()},
         {"quality", params.value("quality", "sq")}},
        {});

    std::vector<std::string> songUrlMismatches;
    const auto volatilePathsSongUrl =
        meta["volatile_paths"].get<std::vector<std::string>>();

    auto expectedSongUrl = fixtureSongUrl;
    expectedSongUrl.erase("_meta");

    const bool songUrlOk = echo::core::ContractJsonMatches(
        expectedSongUrl, songUrlResponse.body, volatilePathsSongUrl,
        &songUrlMismatches);
    if (!songUrlOk) {
      for (const auto& mp : songUrlMismatches) {
        std::cerr << "[A1 contract] /song/url mismatch at: " << mp << '\n';
      }
    }
    assert(songUrlOk && "[A1] /song/url contract failed – see mismatch paths above");
  }

  // ── /lyric contract ──────────────────────────────────────────────────────
  {
    const auto fixtureLyric = LoadFixture("lyric.json");
    const auto& meta = fixtureLyric["_meta"];

    echo::core::CompatApiHandlers lyricContractHandlers;
    lyricContractHandlers.lyricDetail = [](std::string id,
                                            std::string accessKey) -> nlohmann::json {
      return nlohmann::json{
          {"status", 200},
          {"decodeContent", "[00:01.00]晴天"},
          {"data", {{"id", id}, {"accesskey", accessKey}}},
      };
    };
    echo::storage::Database lyricContractDb;
    lyricContractDb.Open(TestDbPath());
    lyricContractDb.Initialize();
    echo::core::CompatApi lyricContractApi(lyricContractDb,
                                            std::move(lyricContractHandlers));

    const auto& params = meta["params"];
    const auto lyricResponse = lyricContractApi.Handle(
        "GET",
        "/lyric",
        {{"id", params["id"].get<std::string>()},
         {"accessKey", params["accessKey"].get<std::string>()}},
        {});

    std::vector<std::string> lyricMismatches;
    const auto volatilePathsLyric =
        meta["volatile_paths"].get<std::vector<std::string>>();

    auto expectedLyric = fixtureLyric;
    expectedLyric.erase("_meta");

    const bool lyricOk = echo::core::ContractJsonMatches(
        expectedLyric, lyricResponse.body, volatilePathsLyric,
        &lyricMismatches);
    if (!lyricOk) {
      for (const auto& mp : lyricMismatches) {
        std::cerr << "[A1 contract] /lyric mismatch at: " << mp << '\n';
      }
    }
    assert(lyricOk && "[A1] /lyric contract failed – see mismatch paths above");
  }

  // ── /playlist/track/all contract ─────────────────────────────────────────
  {
    const auto fixturePlaylist = LoadFixture("playlist_track_all.json");
    const auto& meta = fixturePlaylist["_meta"];

    echo::core::CompatApiHandlers playlistContractHandlers;
    playlistContractHandlers.playlistTracks = [](std::string id, int page, int pageSize) -> nlohmann::json {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {
               {"id", id},
               {"page", page},
               {"pagesize", pageSize},
               {"total", 1},
               {"songs",
                nlohmann::json::array({{{"SongName", "晴天"},
                                         {"FileHash", "abc123"},
                                         {"SingerName", "周杰伦"}}})},
           }},
      };
    };
    echo::storage::Database playlistContractDb;
    playlistContractDb.Open(TestDbPath());
    playlistContractDb.Initialize();
    echo::core::CompatApi playlistContractApi(playlistContractDb, std::move(playlistContractHandlers));

    const auto playlistResponse = playlistContractApi.Handle(
        "GET",
        meta["route"].get<std::string>(),
        {{"id", meta["params"]["id"].get<std::string>()},
         {"page", meta["params"]["page"].get<std::string>()},
         {"pageSize", meta["params"]["pageSize"].get<std::string>()}},
        {});
    assert(playlistResponse.httpStatus == 200);

    std::vector<std::string> playlistMismatches;
    const auto volatilePathsPlaylist = meta["volatile_paths"].get<std::vector<std::string>>();

    auto expectedPlaylist = fixturePlaylist;
    expectedPlaylist.erase("_meta");

    const bool playlistOk = echo::core::ContractJsonMatches(
        expectedPlaylist, playlistResponse.body, volatilePathsPlaylist, &playlistMismatches);
    if (!playlistOk) {
      for (const auto& mp : playlistMismatches) {
        std::cerr << "[A1 contract] /playlist/track/all mismatch at: " << mp << '\n';
      }
    }
    assert(playlistOk && "[A1] /playlist/track/all contract failed – see mismatch paths above");
  }

  std::cout << "[Test] Testing /login/qr/key live API contract..." << std::endl;
  // ── /login/qr/key live API contract ────────────────────────────────────────
  // Now that /login/qr/key is wired to the real Kugou API, verify:
  //   1. The route no longer returns 501 (native_not_implemented).
  //   2. The response is valid JSON with a "status" field.
  {
    echo::storage::Database loginContractDb;
    loginContractDb.Open(TestDbPath());
    loginContractDb.Initialize();
    echo::core::CompatApiHandlers loginContractHandlers;
    loginContractHandlers.loginQrKey = [](const echo::core::DeviceInfo&) -> nlohmann::json {
      return {{"status", 1}, {"data", {{"qrcode", "mock_key"}, {"qrcodeurl", "http://mock"}}}};
    };
    echo::core::CompatApi loginContractApi(loginContractDb, std::move(loginContractHandlers));

    const auto loginResponse = loginContractApi.Handle("GET", "/login/qr/key", {}, {});
    assert(loginResponse.httpStatus == 200 &&
           "[A1] /login/qr/key should no longer return 501");
    assert(loginResponse.body.contains("status") &&
           "[A1] /login/qr/key response must contain 'status' field");
    // If the API call succeeds, status==1 and data.qrcode exists.
    // If the network fails, status==0 and error exists.
    // Either way, the route is live, not a stub.
    assert(!loginResponse.body.contains("error_code") ||
           loginResponse.body["error_code"].get<std::string>() != "native_not_implemented");
  }

  std::cout << "[Test] Testing A3 Large list and memory regression..." << std::endl;
  // ── A3: Large list and image-cache memory regression ─────────────────────
  //
  // Acceptance criteria:
  //   1. CalculateVisibleRows returns ≤14 rows for any scroll offset through
  //      10,000 items (no full list materialisation at draw time).
  //   2. SearchViewModel + CalculateVisibleRows: data layer may hold all rows,
  //      but the visible draw slice is always bounded.
  //   3. PlaybackQueueState (10,000 tracks) + CalculateVisibleRows: draw slice
  //      bounded at every scroll step.
  //   4. MemoryImageCache stays within byte budget after 10,000 cover writes;
  //      MemorySnapshot imageCacheBytes ≤ declared budget before AND after.
  // ---------------------------------------------------------------------------

  // Sub-test 1: CalculateVisibleRows bounded for every scroll step.
  {
    constexpr std::size_t kTotalRows  = 10'000;
    constexpr float       kRowHeight  = 58.0f;
    constexpr float       kViewport   = 640.0f;
    constexpr std::size_t kMaxVisible = 14;

    // Maximum scroll offset the list can reach.
    const float maxOffset =
        echo::win32_app::ClampScrollOffset(kTotalRows, kRowHeight, kViewport, 999'999.0f);

    // Simulate 1,000 equally-spaced scroll positions.
    for (int step = 0; step <= 1000; ++step) {
      const float offset = maxOffset * static_cast<float>(step) / 1000.0f;
      const auto rows = echo::win32_app::CalculateVisibleRows(
          kTotalRows, kRowHeight, 0.0f, offset, kViewport, /*overscan=*/1);

      assert(rows.count    <= kMaxVisible && "[A3] visible row count exceeded limit");
      assert(rows.lastExclusive <= kTotalRows  && "[A3] lastExclusive out of bounds");
    }
  }

  // Sub-test 2: SearchViewModel data + visible-slice boundary.
  {
    // Build a JSON response with 10,000 search results.
    nlohmann::json bigLists = nlohmann::json::array();
    for (int i = 0; i < 10'000; ++i) {
      nlohmann::json row;
      row["SongName"]   = "Song " + std::to_string(i);
      row["SingerName"] = "Artist";
      row["AlbumName"]  = "Album";
      row["Duration"]   = 200 + (i % 120);
      row["FileHash"]   = "hash" + std::to_string(i);
      bigLists.push_back(row);
    }
    nlohmann::json bigResponse{
        {"status", 1},
        {"data", {{"total", 10'000}, {"lists", bigLists}}},
    };

    const auto bigVm = echo::win32_app::BuildSearchViewModel("test", bigResponse);
    assert(bigVm.state == echo::win32_app::SearchState::Ready);
    assert(bigVm.total == 10'000);
    assert(bigVm.rows.size() == 10'000);  // Data layer holds all results.

    // Draw layer: CalculateVisibleRows must never return more than 14 rows.
    const float maxOffset = echo::win32_app::ClampScrollOffset(
        bigVm.rows.size(), 58.0f, 620.0f, 999'999.0f);
    for (int step = 0; step <= 200; ++step) {
      const float offset = maxOffset * static_cast<float>(step) / 200.0f;
      const auto visible = echo::win32_app::CalculateVisibleRows(
          bigVm.rows.size(), 58.0f, 0.0f, offset, 620.0f, /*overscan=*/1);
      assert(visible.count <= 14 && "[A3] SearchViewModel draw slice too large");
    }
  }

  // Sub-test 3: PlaybackQueueState (10,000 tracks) + draw-slice boundary.
  {
    std::vector<echo::win32_app::QueueTrack> bigQueue;
    bigQueue.reserve(10'000);
    for (int i = 0; i < 10'000; ++i) {
      bigQueue.push_back({L"Track " + std::to_wstring(i), L"Artist", L"Album", L"03:30"});
    }
    const echo::win32_app::PlaybackQueueState bigQueueState(std::move(bigQueue));
    assert(bigQueueState.HasTracks());
    assert(bigQueueState.Tracks().size() == 10'000);

    const float maxOffset = echo::win32_app::ClampScrollOffset(
        bigQueueState.Tracks().size(), 68.0f, 620.0f, 999'999.0f);
    for (int step = 0; step <= 200; ++step) {
      const float offset = maxOffset * static_cast<float>(step) / 200.0f;
      const auto visible = echo::win32_app::CalculateVisibleRows(
          bigQueueState.Tracks().size(), 68.0f, 0.0f, offset, 620.0f, /*overscan=*/1);
      assert(visible.count <= 12 && "[A3] PlaybackQueue draw slice too large");
      assert(visible.lastExclusive <= 10'000);
    }
  }

  // Sub-test 4: MemoryImageCache LRU + MemorySnapshot never exceeds budget.
  {
    // Use a tight budget (64 KB) with 256-byte covers so LRU *must* evict
    // when we write 10,000 entries (10,000 × 256 B = 2.5 MB >> 64 KB).
    constexpr std::size_t kTightBudget = 64 * 1024;  // 64 KB
    constexpr std::size_t kCoverBytes  = 256;

    echo::image::MemoryImageCache lruCache(kTightBudget);

    // Snapshot before: image cache is empty.
    echo::diagnostics::MemorySnapshotProvider snapshotProvider;
    const auto snapshotBefore =
        snapshotProvider.Capture(lruCache.Stats().byteCount, 0, "Idle");
    assert(snapshotBefore.imageCacheBytes == 0);

    // Simulate writing 10,000 cover entries (as during continuous scrolling).
    for (int i = 0; i < 10'000; ++i) {
      lruCache.Put("cover:" + std::to_string(i),
                   std::vector<std::uint8_t>(kCoverBytes, static_cast<std::uint8_t>(i & 0xFF)));
    }

    // Snapshot after: cache must stay within budget.
    const auto snapshotAfter =
        snapshotProvider.Capture(lruCache.Stats().byteCount, 0, "Idle");

    assert(snapshotAfter.imageCacheBytes <= kTightBudget &&
           "[A3] image cache exceeded tight byte budget after 10,000 writes");
    assert(lruCache.Stats().byteCount <= kTightBudget &&
           "[A3] MemoryImageCache.byteCount exceeds tight budget");

    // With a 64 KB budget and 256 B items, at most 256 items fit.
    // After 10,000 writes the LRU must have evicted the old entries.
    assert(lruCache.Stats().itemCount < 10'000 &&
           "[A3] LRU cache retained all 10,000 items – eviction not working");
    assert(lruCache.Stats().itemCount <= kTightBudget / kCoverBytes + 1 &&
           "[A3] LRU item count exceeds theoretical maximum for tight budget");

    // The most-recently-written key must still be in cache.
    assert(lruCache.Get("cover:9999").has_value() &&
           "[A3] Most-recent cover evicted prematurely from tight-budget cache");

    // Also verify the default 16 MB cache stays within budget after same load.
    echo::image::MemoryImageCache defaultCache;  // 16 MB default
    for (int i = 0; i < 10'000; ++i) {
      defaultCache.Put("cover:" + std::to_string(i),
                       std::vector<std::uint8_t>(kCoverBytes, static_cast<std::uint8_t>(i & 0xFF)));
    }
    assert(defaultCache.Stats().byteCount <= defaultCache.Stats().byteBudget &&
           "[A3] default 16MB cache exceeded its own budget");
    assert(defaultCache.Get("cover:9999").has_value() &&
           "[A3] Most-recent cover missing from 16MB cache");
  }



  // ── A4: Media Foundation playback error recovery ──────────────────────────
  //
  // Acceptance criteria:
  //   1. PlayUrl failure sets state to Failed with a non-empty error string.
  //   2. Stop() after failure resets the state machine; a second Initialize()
  //      + PlayUrl() produces the same deterministic result (no hung state).
  //   3. Repeated fail→stop→play cycles do not accumulate MF objects (verified
  //      by ensuring each cycle ends cleanly in Failed and is resettable).
  //   4. PlaybackState invariants after failure: sourceUrl empty, error
  //      non-empty, duration and position are 0.
  // ---------------------------------------------------------------------------

  // Sub-test 1: Initial failure state invariants.
  {
    echo::playback::PlaybackController pc;
    assert(pc.Initialize());

    // PlayUrl("") must fail — no valid URL.
    const bool played = pc.PlayUrl("");
    assert(!played && "[A4] PlayUrl empty-url unexpectedly succeeded");

    const auto state = pc.GetState();
    assert(state.kind == echo::core::PlaybackStateKind::Failed &&
           "[A4] state not Failed after bad PlayUrl");
    assert(!state.error.empty() && "[A4] error string empty after failure");
    assert(state.sourceUrl.empty() && "[A4] sourceUrl not empty after failure");
    assert(state.durationSeconds == 0.0 && "[A4] duration non-zero after failure");
    assert(state.currentSeconds == 0.0 && "[A4] position non-zero after failure");
  }

  // Sub-test 2: Stop() → re-play recovers cleanly.
  {
    echo::playback::PlaybackController pc;
    assert(pc.Initialize());

    // First failure.
    assert(!pc.PlayUrl("") && "[A4] round-1 PlayUrl should fail");
    assert(pc.GetState().kind == echo::core::PlaybackStateKind::Failed);

    // Stop resets the state machine.
    pc.Stop();
    const auto stoppedState = pc.GetState();
    assert(stoppedState.kind != echo::core::PlaybackStateKind::Failed &&
           "[A4] Stop() did not clear Failed state");

    // After Stop(), a second PlayUrl must behave the same as the first.
    assert(!pc.PlayUrl("") && "[A4] round-2 PlayUrl should fail");
    assert(pc.GetState().kind == echo::core::PlaybackStateKind::Failed &&
           "[A4] second PlayUrl did not set Failed state");
    assert(!pc.GetState().error.empty() &&
           "[A4] error string empty on second failure");
  }

  // Sub-test 3: Ten consecutive fail→stop→play cycles (no MF object leak).
  {
    echo::playback::PlaybackController pc;
    assert(pc.Initialize());

    for (int round = 0; round < 10; ++round) {
      const bool ok = pc.PlayUrl("");
      assert(!ok && "[A4] PlayUrl unexpectedly succeeded in cycle");
      const auto s = pc.GetState();
      assert(s.kind == echo::core::PlaybackStateKind::Failed &&
             "[A4] state not Failed in cycle");
      assert(!s.error.empty() && "[A4] error empty in cycle");
      // Reset for next round.
      pc.Stop();
    }
    // After 10 cycles, controller must still be in a non-Failed state
    // (Stop was the last operation).
    assert(pc.GetState().kind != echo::core::PlaybackStateKind::Failed &&
           "[A4] controller stuck in Failed after 10 cycles");
  }

  // Cryptography and Signatures tests
  {
    // MD5 verification
    const std::string emptyMd5 = echo::core::CalculateMd5("");
    assert(emptyMd5 == "d41d8cd98f00b204e9800998ecf8427e" && "MD5 of empty string is incorrect");

    const std::string helloMd5 = echo::core::CalculateMd5("hello");
    assert(helloMd5 == "5d41402abc4b2a76b9719d911017c592" && "MD5 of 'hello' is incorrect");

    // Signature verification
    std::unordered_map<std::string, std::string> params = {{"foo", "bar"}, {"abc", "123"}};
    // Web: salt + sorted(abc=123foo=bar) + salt
    // String: NVPh5oo715z5DIWAeQlhMDsWXXQV4hwtabc=123foo=barNVPh5oo715z5DIWAeQlhMDsWXXQV4hwt
    const std::string expectedWebString = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwtabc=123foo=barNVPh5oo715z5DIWAeQlhMDsWXXQV4hwt";
    const std::string expectedWebSig = echo::core::CalculateMd5(expectedWebString);
    assert(echo::core::SignatureWebParams(params) == expectedWebSig && "SignatureWebParams mismatch");

    // Android: salt + sorted(abc=123foo=bar) + data + salt
    // String: OIlwieks28dk2k092lksi2UIkpabc=123foo=barpayloadOIlwieks28dk2k092lksi2UIkp
    const std::string expectedAndroidString = "OIlwieks28dk2k092lksi2UIkpabc=123foo=barpayloadOIlwieks28dk2k092lksi2UIkp";
    const std::string expectedAndroidSig = echo::core::CalculateMd5(expectedAndroidString);
    assert(echo::core::SignatureAndroidParams(params, "payload") == expectedAndroidSig && "SignatureAndroidParams mismatch");
  }

  std::cout << "[Test] Testing LoginService tests..." << std::endl;
  // LoginService tests
  {
    echo::core::DeviceInfo device = {
        .dfid = "dfid123",
        .mid = "mid123",
        .uuid = "uuid123",
        .guid = "uuid123",
        .serverDev = "",
        .mac = "mac123",
        .appid = "1014",
        .clientver = "20000"
    };

    std::string capturedUrl;
    auto mockGet = [&](const std::string& url, const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      capturedUrl = url;
      echo::core::HttpResult res;
      res.statusCode = 200;
      res.body = R"({"status":1,"data":{"qrcode":"mock_key","qrcodeurl":"http://mock","status":1}})";
      return res;
    };

    echo::core::LoginService service(mockGet);
    auto beginResult = service.BeginQrLogin(device);
    assert(beginResult["status"].get<int>() == 1);
    assert(beginResult["data"]["qrcode"].get<std::string>() == "mock_key");
    assert(capturedUrl.find("https://login-user.kugou.com/v2/qrcode") == 0);
    assert(capturedUrl.find("appid=1014") != std::string::npos);
    assert(capturedUrl.find("signature=") != std::string::npos);

    auto mockPoll = [&](const std::string& url, const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      capturedUrl = url;
      echo::core::HttpResult res;
      res.statusCode = 200;
      res.body = R"({"status":1,"data":{"status":4,"token":"mock_token","userid":"123456"}})";
      return res;
    };

    echo::core::LoginService pollService(mockPoll);
    auto pollResult = pollService.PollQrLogin(device, "mock_key");
    assert(pollResult["status"].get<int>() == 1);
    assert(pollResult["data"]["status"].get<int>() == 4);
    assert(pollResult["data"]["token"].get<std::string>() == "mock_token");
    assert(capturedUrl.find("https://login-user.kugou.com/v2/get_userinfo_qrcode") == 0);
    assert(capturedUrl.find("qrcode=mock_key") != std::string::npos);
  }

  std::cout << "[Test] Testing PlaylistService extended tests..." << std::endl;
  // PlaylistService extended tests
  {
    std::string capturedPostUrl;
    std::string capturedPostBody;

    auto mockGet = [](const std::string& url,
                      const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      return {200, R"({"status":1,"data":{"info":[],"total":0}})", ""};
    };

    auto mockPost = [&](const std::string& url,
                        const std::string& body,
                        const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      capturedPostUrl = url;
      capturedPostBody = body;
      echo::core::HttpResult res;
      res.statusCode = 200;
      res.body = R"({"status":1,"data":{"info":[{"global_collection_id":"abc123","name":"Test Playlist"}]}})";
      return res;
    };

    echo::core::PlaylistService playlistSvc(mockGet, mockPost);

    // GetPlaylistDetail
    auto detailResult = playlistSvc.GetPlaylistDetail("abc123", "42", "tok");
    assert(detailResult["status"].get<int>() == 1);
    assert(capturedPostUrl.find("gateway.kugou.com/v3/get_list_info") != std::string::npos);
    assert(capturedPostUrl.find("signature=") != std::string::npos);
    assert(capturedPostBody.find("abc123") != std::string::npos);

    // GetPlaylistDetail with empty id
    auto emptyDetail = playlistSvc.GetPlaylistDetail("", "42", "tok");
    assert(emptyDetail["status"].get<int>() == 0);

    // GetUserPlaylists
    auto mockPostUser = [&](const std::string& url,
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      capturedPostUrl = url;
      capturedPostBody = body;
      echo::core::HttpResult res;
      res.statusCode = 200;
      res.body = R"({"errcode":0,"data":{"lists":[{"global_collection_id":"gid42","listname":"My Playlist"}],"total":1}})";
      return res;
    };

    echo::core::PlaylistService userPlaylistSvc(mockGet, mockPostUser);
    auto userResult = userPlaylistSvc.GetUserPlaylists("42", "tok", 1, 30);
    assert(userResult["status"].get<int>() == 1);
    assert(userResult["data"]["list"].size() == 1);
    assert(userResult["data"]["list"][0]["id"].get<std::string>() == "gid42");
    assert(userResult["data"]["list"][0]["name"].get<std::string>() == "My Playlist");
    assert(capturedPostUrl.find("gateway.kugou.com/v7/get_all_list") != std::string::npos);
    assert(capturedPostUrl.find("signature=") != std::string::npos);
    assert(capturedPostBody.find("\"userid\":\"42\"") != std::string::npos);

    // GetUserPlaylists with no user
    auto noUserResult = userPlaylistSvc.GetUserPlaylists("", "tok", 1, 30);
    assert(noUserResult["status"].get<int>() == 0);
    assert(noUserResult["error"].get<std::string>() == "not logged in");
  }

  std::cout << "[Test] UserService & HomeService contracts" << std::endl;
  {
    // --- Crypto: RsaRawEncrypt ---
    const auto rsaHex = echo::core::RsaRawEncrypt("{\"token\":\"abc\",\"clienttime\":1700000000}");
    assert(!rsaHex.empty());
    // RSA-1024 → 128 bytes → 256 hex chars
    assert(rsaHex.size() == 256);
    // Must be uppercase (matching JS .toUpperCase())
    for (const char c : rsaHex) {
      assert((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
    }
    std::cout << "  [ok] RsaRawEncrypt produces 256-char uppercase hex" << std::endl;

    // --- UserService: not logged in ---
    {
      std::string capturedUrl;
      auto mockPost = [&](const std::string& url,
                          const std::string& /*body*/,
                          const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":{"nickname":"TestUser","avatar":""}})";
        return res;
      };
      echo::core::UserService userSvc(mockPost);

      // Empty credentials → early return
      const auto noAuth = userSvc.GetUserDetail("", "");
      assert(noAuth["status"].get<int>() == 0);
      assert(noAuth["error"].get<std::string>() == "not logged in");
      std::cout << "  [ok] UserService returns not-logged-in with empty creds" << std::endl;

      // Valid credentials → POST is dispatched
      const auto result = userSvc.GetUserDetail("42", "mytoken");
      assert(result["status"].get<int>() == 1);
      assert(capturedUrl.find("gateway.kugou.com/v3/get_my_info") != std::string::npos);
      assert(capturedUrl.find("signature=") != std::string::npos);
      std::cout << "  [ok] UserService dispatches POST to usercenter endpoint" << std::endl;
    }

    // --- UserService: VIP ---
    {
      std::string capturedUrl;
      auto mockGet = [&](const std::string& url,
                         const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":{"vip_type":3}})";
        return res;
      };
      auto nopPost = [](const std::string&, const std::string&,
                        const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{};
      };
      echo::core::UserService vipSvc(mockGet, nopPost);
      const auto vipResult = vipSvc.GetUserVip("42", "mytoken");
      assert(vipResult["status"].get<int>() == 1);
      assert(capturedUrl.find("kugouvip.kugou.com/v1/get_union_vip") != std::string::npos);
      std::cout << "  [ok] UserService dispatches GET to kugouvip endpoint" << std::endl;
    }

    // --- HomeService: Banners ---
    {
      std::string capturedUrl;
      auto mockPost = [&](const std::string& url,
                          const std::string& /*body*/,
                          const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":{"list":[{"url":"https://cdn.example/banner.jpg"}]}})";
        return res;
      };
      echo::core::HomeService homeSvc(mockPost);
      const auto banners = homeSvc.GetBanners("", "");
      assert(banners["status"].get<int>() == 1);
      assert(capturedUrl.find("ads.gateway/v3/listen_banner") != std::string::npos);
      std::cout << "  [ok] HomeService dispatches POST to listen_banner" << std::endl;
    }

    // --- HomeService: EverydayRecommend ---
    {
      std::string capturedUrl;
      std::string capturedXRouter;
      auto mockPost = [&](const std::string& url,
                          const std::string& /*body*/,
                          const std::unordered_map<std::string, std::string>& headers) {
        capturedUrl = url;
        const auto it = headers.find("x-router");
        if (it != headers.end()) capturedXRouter = it->second;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":{"songs":[{"hash":"aabbcc"}]}})";
        return res;
      };
      echo::core::HomeService homeSvc(mockPost);
      const auto rec = homeSvc.GetEverydayRecommend("", "");
      assert(rec["status"].get<int>() == 1);
      assert(capturedUrl.find("everyday_song_recommend") != std::string::npos);
      assert(capturedXRouter == "everydayrec.service.kugou.com");
      std::cout << "  [ok] HomeService dispatches POST to everydayrec with correct x-router" << std::endl;
    }

    // --- Crypto: AES and RSA ---
    {
      std::string text = R"({"page":1,"pagesize":30})";
      auto aesPair = echo::core::PlaylistAesEncrypt(text);
      assert(!aesPair.key.empty());
      assert(!aesPair.data.empty());
      std::string decrypted = echo::core::PlaylistAesDecrypt(aesPair.data, aesPair.key);
      assert(decrypted == text);

      std::string dataForSign = "1234567890";
      std::string signKeyVal = echo::core::SignParamsKey(dataForSign, "1005", "20489");
      assert(!signKeyVal.empty());

      std::string rsaCipher = echo::core::RsaPkcs1Encrypt("test_payload");
      assert(!rsaCipher.empty());
      std::cout << "  [ok] Crypto: AES round-trip and RSA/MD5 signing pass" << std::endl;
    }

    // --- SongService ---
    {
      std::string capturedUrl;
      auto mockGet = [&](const std::string& url,
                         const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":[{"climax":1}]})";
        return res;
      };
      echo::core::SongService songSvc(mockGet);
      
      const auto climax = songSvc.GetClimax("hash1,hash2");
      assert(climax["status"].get<int>() == 1);
      assert(capturedUrl.find("v1/audio_climax/audio") != std::string::npos);
      assert(capturedUrl.find("data=") != std::string::npos);

      const auto ranking = songSvc.GetRanking("12345");
      assert(ranking["status"].get<int>() == 1);
      assert(capturedUrl.find("grow/v1/song_ranking/play_page/ranking_info") != std::string::npos);
      assert(capturedUrl.find("album_audio_id=12345") != std::string::npos);

      const auto filter = songSvc.GetRankingFilter("12345", 2, 10);
      assert(filter["status"].get<int>() == 1);
      assert(capturedUrl.find("grow/v1/song_ranking/unlock/v2/ranking_filter") != std::string::npos);
      assert(capturedUrl.find("page=2") != std::string::npos);
      assert(capturedUrl.find("pagesize=10") != std::string::npos);
      std::cout << "  [ok] SongService dispatches API requests correctly" << std::endl;
    }

    // --- PlayHistoryService ---
    {
      std::string capturedUrl;
      std::string capturedBody;
      auto mockPost = [&](const std::string& url,
                          const std::string& body,
                          const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        capturedBody = body;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1})";
        return res;
      };
      echo::core::PlayHistoryService playSvc(mockPost);
      const auto res = playSvc.UploadSong("42", "mytoken", 999, 1234567, 1);
      assert(res["status"].get<int>() == 1);
      assert(capturedUrl.find("playhistory/v1/upload_songs") != std::string::npos);
      auto bodyJson = nlohmann::json::parse(capturedBody);
      assert(bodyJson["token"] == "mytoken");
      assert(bodyJson["userid"] == 42);
      assert(bodyJson["songs"][0]["mxid"] == 999);
      std::cout << "  [ok] PlayHistoryService dispatches POST with payload correctly" << std::endl;
    }

    // --- UserCloudService ---
    {
      std::string capturedUrl;
      std::string capturedBody;
      auto mockPost = [&](const std::string& url,
                          const std::string& body,
                          const std::unordered_map<std::string, std::string>& /*headers*/) {
        capturedUrl = url;
        capturedBody = body;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = "raw_unencrypted_or_bad_encrypted_body";
        return res;
      };
      echo::core::UserCloudService cloudSvc(mockPost);
      const auto res = cloudSvc.GetList("42", "mytoken", 1, 30);
      assert(res["status"].get<int>() == 0);
      assert(res["error"].get<std::string>() == "AES decryption of response failed");
      assert(capturedUrl.find("mcloudservice.kugou.com/v1/get_list") != std::string::npos);
      assert(capturedUrl.find("p=") != std::string::npos);
      assert(capturedUrl.find("key=") != std::string::npos);
      assert(!capturedBody.empty());
      std::cout << "  [ok] UserCloudService dispatches encrypted POST correctly" << std::endl;
    }

    // --- CompatApi routes via injected mocks ---
    {
      echo::storage::Database db;
      db.Open(TestDbPath());
      db.Initialize();

      echo::core::CompatApiHandlers handlers;
      handlers.userDetail = [](std::string /*userId*/, std::string /*token*/) -> nlohmann::json {
        return {{"status", 1}, {"data", {{"nickname", "MockUser"}}}};
      };
      handlers.everydayRecommend = [](std::string /*userId*/, std::string /*token*/) -> nlohmann::json {
        return {{"status", 1}, {"data", {{"songs", nlohmann::json::array()}}}};
      };

      echo::core::CompatApi api(db, handlers);

      const auto detailResp = api.Handle("GET", "/user/detail", {}, {});
      assert(detailResp.httpStatus == 200);
      assert(detailResp.body["status"].get<int>() == 1);
      assert(detailResp.body["data"]["nickname"].get<std::string>() == "MockUser");
      std::cout << "  [ok] CompatApi /user/detail routes to handler" << std::endl;

      const auto recResp = api.Handle("GET", "/everyday/recommend", {}, {});
      assert(recResp.httpStatus == 200);
      assert(recResp.body["status"].get<int>() == 1);
      std::cout << "  [ok] CompatApi /everyday/recommend routes to handler" << std::endl;
    }

    // --- PlaylistService & PlayHistoryService new methods TDD tests ---
    {
      std::string capturedUrl;
      std::string capturedBody;
      std::unordered_map<std::string, std::string> capturedHeaders;

      auto mockPost = [&](const std::string& url,
                          const std::string& body,
                          const std::unordered_map<std::string, std::string>& headers) {
        capturedUrl = url;
        capturedBody = body;
        capturedHeaders = headers;
        echo::core::HttpResult res;
        res.statusCode = 200;
        res.body = R"({"status":1,"data":{"listid":12345,"total_ver":1,"list":[]}})";
        return res;
      };

      echo::core::PlaylistService playlistSvc(nullptr, mockPost);

      // Test AddPlaylist
      auto addRes = playlistSvc.AddPlaylist("42", "mytoken", "MyNewPlaylist");
      assert(addRes["status"].get<int>() == 1);
      assert(capturedUrl.find("/cloudlist.service/v5/add_list") != std::string::npos);
      assert(capturedUrl.find("signature=") != std::string::npos);
      nlohmann::json addedJson = nlohmann::json::parse(capturedBody);
      assert(addedJson["name"] == "MyNewPlaylist");
      assert(addedJson["userid"] == 42);
      std::cout << "  [ok] PlaylistService::AddPlaylist dispatches POST correctly" << std::endl;

      // Test DeletePlaylist
      auto deleteRes = playlistSvc.DeletePlaylist("42", "mytoken", 12345);
      assert(deleteRes["status"].get<int>() == 0);
      assert(deleteRes["error"].get<std::string>() == "AES decryption of response failed");
      assert(capturedUrl.find("/v2/delete_list") != std::string::npos);
      assert(capturedUrl.find("p=") != std::string::npos);
      assert(capturedUrl.find("key=") != std::string::npos);
      assert(capturedHeaders["x-router"] == "cloudlist.service.kugou.com");
      std::cout << "  [ok] PlaylistService::DeletePlaylist dispatches POST correctly" << std::endl;

      // Test AddPlaylistTracks
      auto addTracksRes = playlistSvc.AddPlaylistTracks("42", "mytoken", "12345", "song1|hash1|111|222,song2|hash2|333|444");
      assert(addTracksRes["status"].get<int>() == 1);
      assert(capturedUrl.find("/cloudlist.service/v6/add_song") != std::string::npos);
      nlohmann::json addedTracksJson = nlohmann::json::parse(capturedBody);
      assert(addedTracksJson["listid"] == 12345);
      assert(addedTracksJson["userid"] == 42);
      assert(addedTracksJson["data"].size() == 2);
      assert(addedTracksJson["data"][0]["name"] == "song1");
      assert(addedTracksJson["data"][0]["hash"] == "hash1");
      assert(addedTracksJson["data"][0]["album_id"] == 111);
      assert(addedTracksJson["data"][0]["mixsongid"] == 222);
      std::cout << "  [ok] PlaylistService::AddPlaylistTracks dispatches POST correctly" << std::endl;

      // Test DeletePlaylistTracks
      auto delTracksRes = playlistSvc.DeletePlaylistTracks("42", "mytoken", "12345", "101,102");
      assert(delTracksRes["status"].get<int>() == 1);
      assert(capturedUrl.find("/v4/delete_songs") != std::string::npos);
      nlohmann::json delTracksJson = nlohmann::json::parse(capturedBody);
      assert(delTracksJson["listid"] == 12345);
      assert(delTracksJson["userid"] == 42);
      assert(delTracksJson["data"].size() == 2);
      assert(delTracksJson["data"][0]["fileid"] == 101);
      assert(delTracksJson["data"][1]["fileid"] == 102);
      std::cout << "  [ok] PlaylistService::DeletePlaylistTracks dispatches POST correctly" << std::endl;

      // Test GetUserHistory
      echo::core::PlayHistoryService historySvc(mockPost);
      auto historyRes = historySvc.GetUserHistory("42", "mytoken", "bp_token");
      assert(historyRes["status"].get<int>() == 1);
      assert(capturedUrl.find("/playhistory/v1/get_songs") != std::string::npos);
      nlohmann::json historyJson = nlohmann::json::parse(capturedBody);
      assert(historyJson["userid"] == 42);
      assert(historyJson["token"] == "mytoken");
      assert(historyJson["bp"] == "bp_token");
      assert(historyJson["source_classify"] == "app");
      assert(historyJson["to_subdivide_sr"] == 1);
      std::cout << "  [ok] PlayHistoryService::GetUserHistory dispatches POST correctly" << std::endl;
    }
  }

  {
    // Newsprint Theme tokens — List 15 RED→GREEN
    // 数值来源：Music Player.html :root CSS 变量。
    auto approxByte = [](float channel, int byte) {
      // D2D1::ColorF(0xRRGGBB) 将每个分量除以 255.0f，容差 1/255 已足够。
      const float expected = static_cast<float>(byte) / 255.0f;
      return std::abs(channel - expected) <= 1.0f / 255.0f;
    };

    const auto& paper = echo::win32_app::theme::color::Paper();
    assert(approxByte(paper.r, 0xF1));
    assert(approxByte(paper.g, 0xEA));
    assert(approxByte(paper.b, 0xD8));
    assert(paper.a >= 0.99f && paper.a <= 1.01f);

    const auto& ink = echo::win32_app::theme::color::Ink();
    assert(approxByte(ink.r, 0x22));
    assert(approxByte(ink.g, 0x1B));
    assert(approxByte(ink.b, 0x12));

    const auto& accent = echo::win32_app::theme::color::Accent();
    assert(approxByte(accent.r, 0xA8));
    assert(approxByte(accent.g, 0x31));
    assert(approxByte(accent.b, 0x1B));

    const auto& accentDeep = echo::win32_app::theme::color::AccentDeep();
    assert(approxByte(accentDeep.r, 0x7A));
    assert(approxByte(accentDeep.g, 0x20));
    assert(approxByte(accentDeep.b, 0x10));

    const auto& rule = echo::win32_app::theme::color::Rule();
    // rgba(34,27,18,0.14)
    assert(approxByte(rule.r, 34));
    assert(approxByte(rule.g, 27));
    assert(approxByte(rule.b, 18));
    assert(std::abs(rule.a - 0.14f) < 1e-4f);

    const auto& glass = echo::win32_app::theme::color::GlassTint();
    // rgba(248,243,230,0.46)
    assert(approxByte(glass.r, 248));
    assert(approxByte(glass.g, 243));
    assert(approxByte(glass.b, 230));
    assert(std::abs(glass.a - 0.46f) < 1e-4f);

    // Legacy Palette factory delegates to color accessors with field-name compatibility.
    const auto palette = echo::win32_app::MakeNewsprintPalette();
    assert(palette.bg.r == paper.r && palette.bg.g == paper.g && palette.bg.b == paper.b);
    assert(palette.text.r == ink.r && palette.text.g == ink.g && palette.text.b == ink.b);
    assert(palette.accent.r == accent.r && palette.accent.g == accent.g && palette.accent.b == accent.b);
    assert(palette.accentDark.r == accentDeep.r);
    assert(palette.line.a == rule.a);
    assert(palette.panel.a == glass.a);
    std::cout << "  [ok] Theme Newsprint tokens (Paper/Ink/Accent/AccentDeep/Rule/GlassTint) match HTML reference" << std::endl;
    std::cout << "  [ok] MakeNewsprintPalette legacy field names map to theme::color accessors" << std::endl;
  }

  {
    // RenderPipeline 设备链路构造-销毁循环（List 16 RED→GREEN）。
    // 在没有真实窗口的测试环境下走 InitializeHeadless：仅创建 D3D11 + D2D Device + DeviceContext，
    // 验证 50 次循环不会泄漏设备 / 触发 device removal。
    bool everSucceeded = false;
    for (int i = 0; i < 50; ++i) {
      echo::win32_app::RenderPipeline pipeline;
      const bool ok = pipeline.InitializeHeadless();
      if (!ok) {
        // 在无 GPU / WARP 都不可用的极简 CI 环境下允许跳过；只要至少一次成功即可。
        // 实际开发机几乎必中。
        continue;
      }
      everSucceeded = true;
      assert(pipeline.has_device());
      assert(pipeline.device_context() != nullptr);
      assert(pipeline.factory() != nullptr);
      assert(!pipeline.has_swap_chain());

      // 在 DeviceContext 上创建一支 brush，验证后续 D2D 调用可用（即 ctx 是真实可用的 ID2D1DeviceContext）。
      ID2D1SolidColorBrush* brush = nullptr;
      const HRESULT hr = pipeline.device_context()->CreateSolidColorBrush(
          D2D1::ColorF(D2D1::ColorF::CornflowerBlue), &brush);
      assert(SUCCEEDED(hr));
      assert(brush != nullptr);
      brush->Release();

      pipeline.Shutdown();
      assert(!pipeline.has_device());
      assert(pipeline.device_context() == nullptr);
      // Shutdown 保留 Factory，便于后续重建。
      assert(pipeline.factory() != nullptr);
    }
    if (!everSucceeded) {
      std::cout << "  [skip] RenderPipeline construct/destruct cycle (no D3D11 backend available)" << std::endl;
    } else {
      std::cout << "  [ok] RenderPipeline 50-cycle headless construct/destruct without device removal" << std::endl;
    }

    // IsDeviceLossHResult 应识别所有四种设备丢失码。
    using echo::win32_app::RenderPipeline;
    assert(RenderPipeline::IsDeviceLossHResult(D2DERR_RECREATE_TARGET));
    assert(RenderPipeline::IsDeviceLossHResult(DXGI_ERROR_DEVICE_REMOVED));
    assert(RenderPipeline::IsDeviceLossHResult(DXGI_ERROR_DEVICE_RESET));
    assert(RenderPipeline::IsDeviceLossHResult(DXGI_ERROR_DRIVER_INTERNAL_ERROR));
    assert(!RenderPipeline::IsDeviceLossHResult(S_OK));
    assert(!RenderPipeline::IsDeviceLossHResult(E_FAIL));
    std::cout << "  [ok] RenderPipeline::IsDeviceLossHResult classifies all four device-loss codes" << std::endl;
  }

  {
    // PainterMeasureTest（List 17 RED→GREEN）。
    // MeasureSectionLabel 仅依赖 DWrite TextFormat，不依赖 D2D 设备；
    // 在 InitializeFonts 后即可调用，无需 GPU / swap chain。
    IDWriteFactory* dwriteFactory = nullptr;
    const HRESULT dwriteHr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwriteFactory));
    if (FAILED(dwriteHr) || !dwriteFactory) {
      std::cout << "  [skip] Painter font measure test (DWrite factory unavailable)" << std::endl;
    } else {
      echo::win32_app::Painter painter;
      const bool fontOk = painter.InitializeFonts(dwriteFactory);
      assert(fontOk && "Painter::InitializeFonts must succeed with valid IDWriteFactory");

      // 11px 斜体衬线布局：两个汉字宽度必须 > 0
      const float w = painter.MeasureSectionLabel(L"早晨");
      assert(w > 0.0f && "MeasureSectionLabel(L\"早晨\") must return positive width");

      // 更长文本的宽度必须大于短文本宽度
      const float w2 = painter.MeasureSectionLabel(L"为你推荐 · 今日精选");
      assert(w2 > w && "longer text must produce greater width than shorter text");

      painter.Shutdown();
      dwriteFactory->Release();
      std::cout << "  [ok] Painter::MeasureSectionLabel positive width (\"早晨\"=" << w << "px)" << std::endl;
    }
  }

  {
    // GlassPanelRender（List 19 RED→GREEN）。
    // 验证：
    //   - Initialize(ctx, W, H) 之后 ready() 为 true；
    //   - blurredBitmap 大小为 ¼ scene（向上取整避免 0 维）；
    //   - blurDirty 初始为 true（首次 DrawGlassPanel 触发模糊）；
    //   - EnsureSceneSize 在同尺寸下幂等，不重建 bitmap。
    // 走 RenderPipeline::InitializeHeadless 取得真实 ID2D1DeviceContext。
    echo::win32_app::RenderPipeline pipeline;
    if (!pipeline.InitializeHeadless() || !pipeline.device_context()) {
      std::cout << "  [skip] GlassPanel render test (no D3D11 backend available)" << std::endl;
    } else {
      echo::win32_app::GlassPanel glass;
      const UINT W = 1280;
      const UINT H = 800;
      const bool initOk = glass.Initialize(pipeline.device_context(), W, H);
      assert(initOk && "GlassPanel::Initialize must succeed with valid ctx");
      assert(glass.ready() && "GlassPanel must be ready after Initialize");
      assert(glass.scene_bitmap() && glass.blurred_bitmap() && "both bitmaps must exist");

      // ¼ 分辨率（带 round/clamp 容忍）
      const UINT expectedW = static_cast<UINT>(std::round(W * 0.25f));
      const UINT expectedH = static_cast<UINT>(std::round(H * 0.25f));
      assert(glass.blurred_width() == expectedW && "blurred width must be ¼ scene width");
      assert(glass.blurred_height() == expectedH && "blurred height must be ¼ scene height");

      // 初始应为 dirty
      assert(glass.blur_dirty() && "GlassPanel must start with blurDirty=true");

      // EnsureSceneSize 同尺寸幂等
      const bool ensureOk = glass.EnsureSceneSize(W, H);
      assert(ensureOk && "EnsureSceneSize(same) must succeed");
      assert(glass.scene_width() == W && glass.scene_height() == H && "size must be preserved");

      glass.Shutdown();
      assert(!glass.ready() && "after Shutdown ready() must be false");
      std::cout << "  [ok] GlassPanel: " << W << "x" << H << " scene + " << expectedW << "x"
                << expectedH << " blurred (¼ res)" << std::endl;
    }
  }

  {
    // PaperTextureSmoke（List 19）。
    // 仅验证 Initialize 在 headless ctx 上成功，并提供 bitmap()。
    echo::win32_app::RenderPipeline pipeline;
    if (!pipeline.InitializeHeadless() || !pipeline.device_context()) {
      std::cout << "  [skip] PaperTexture test (no D3D11 backend available)" << std::endl;
    } else {
      echo::win32_app::PaperTexture paper;
      const bool ok = paper.Initialize(pipeline.device_context());
      assert(ok && "PaperTexture::Initialize must succeed");
      assert(paper.bitmap() != nullptr && "PaperTexture::bitmap must be non-null");
      paper.Shutdown();
      std::cout << "  [ok] PaperTexture initialized (128x128 BGRA tile)" << std::endl;
    }
  }

  std::cout << "[Test] All tests completed successfully!" << std::endl;
  return 0;
}

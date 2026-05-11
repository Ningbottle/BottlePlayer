#include <cassert>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <iostream>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#include "echo/async/EventQueue.h"
#include "echo/async/TaskScheduler.h"
#include "echo/core/Authorization.h"
#include "echo/core/BackendFacade.h"
#include "echo/core/CatalogService.h"
#include "echo/core/CompatApi.h"
#include "echo/core/JsonHelpers.h"
#include "echo/core/LyricParser.h"
#include "echo/core/LyricService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/PrivilegeService.h"
#include "echo/core/RankService.h"
#include "echo/core/SearchService.h"
#include "echo/core/SongUrlService.h"
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
#include "echo/win32_app/SearchInput.h"
#include "echo/win32_app/SearchViewModel.h"

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

  echo::core::CompatApi api(database);
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

  auto notImplemented = api.Handle("GET", "/login/qr/key", {}, {});
  assert(notImplemented.httpStatus == 501);
  assert(notImplemented.body["status"] == 0);
  assert(notImplemented.body["error_code"] == "native_not_implemented");

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

  echo::playback::PlaybackController playback;
  assert(playback.Initialize());
  assert(playback.PlayUrl("https://example.invalid/audio.mp3"));
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
  assert(playback.PlayUrl("https://example.invalid/next.mp3"));
  assert(playback.GetState().sourceUrl == "https://example.invalid/next.mp3");
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
  assert(melodyLayout.header.bottom == 86.0f);
  assert(melodyLayout.playerBar.top == 964.0f);
  assert(melodyLayout.content.left == 178.0f);
  assert(melodyLayout.content.top == 86.0f);
  assert(melodyLayout.content.bottom == 964.0f);
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
  assert(realSmallPlayerBar.bar.bottom == 589.0f);
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
  assert(echo::win32_app::HitTestSidebar(40.0f, 116.0f, 900.0f) ==
         echo::win32_app::SidebarAction::Home);
  assert(echo::win32_app::HitTestSidebar(40.0f, 456.0f, 900.0f) ==
         echo::win32_app::SidebarAction::NowPlaying);
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
  assert(roomyPlayerBar.progress.right - roomyPlayerBar.progress.left >= 500.0f);

  const auto compactCards = echo::win32_app::CalculateCardStripLayout(704.0f, 5, 210.0f);
  assert(compactCards.count == 4);
  assert(compactCards.itemWidth >= 140.0f);
  assert(compactCards.itemWidth <= 190.0f);
  assert(compactCards.imageHeight >= compactCards.itemWidth * 0.62f);

  const auto shortCards = echo::win32_app::CalculateCardStripLayout(704.0f, 5, 150.0f);
  assert(shortCards.count == 4);
  assert(shortCards.imageHeight <= shortCards.itemHeight - 58.0f);
  assert(shortCards.imageHeight >= 72.0f);

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
                      {"Image", "https://img.example/jay-cover.jpg"}},
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
  assert(searchVm.rows[0].coverUrl == "https://img.example/jay-cover.jpg");
  assert(searchVm.rows[0].imageKey == "remote-cover:https://img.example/jay-cover.jpg");

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

  return 0;
}

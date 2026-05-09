#include <cassert>
#include <atomic>
#include <fstream>
#include <filesystem>

#include "echo/async/EventQueue.h"
#include "echo/async/TaskScheduler.h"
#include "echo/core/Authorization.h"
#include "echo/core/BackendFacade.h"
#include "echo/core/CatalogService.h"
#include "echo/core/CompatApi.h"
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
#include "echo/win32_app/Layout.h"
#include "echo/win32_app/LyricViewModel.h"
#include "echo/win32_app/PlaybackViewModel.h"
#include "echo/win32_app/SearchViewModel.h"

namespace {

std::filesystem::path TestDbPath() {
  auto path = std::filesystem::temp_directory_path() / L"echomusic-native-tests.db";
  std::filesystem::remove(path);
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
                      {"FileHash", "abc"}},
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

  const auto playFailed = echo::win32_app::BuildPlaybackViewModel(
      searchVm.rows[0],
      nlohmann::json{{"status", 0}, {"error", "需要付费"}});
  assert(playFailed.state == echo::win32_app::PlayerUiState::Error);
  assert(playFailed.error == L"需要付费");
  assert(playFailed.sourceUrl.empty());

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

  const auto emptyLyricVm = echo::win32_app::BuildLyricViewModel(noLyric, 0);
  assert(emptyLyricVm.state == echo::win32_app::LyricUiState::Empty);
  assert(emptyLyricVm.message == L"暂无歌词");

  return 0;
}

#include "echo/core/CatalogService.h"
#include "echo/core/PlaylistService.h"
#include "echo/core/RankService.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Discovery contract failed at line " << __LINE__ << '\n'; return 1; } } while (false)

int main() {
  using echo::core::HttpResult;
  using nlohmann::json;
  std::string body;
  auto get = [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
    return HttpResult{200, body, ""};
  };
  echo::core::CatalogService catalog(get);
  echo::core::RankService rank(get);
  echo::core::PlaylistService playlists(get);
  const std::vector<std::function<json()>> methods = {
      [&] { return catalog.GetAlbumDetail("1"); },
      [&] { return catalog.GetAlbumSongs("1", 1, 20); },
      [&] { return catalog.GetArtistDetail("1"); },
      [&] { return catalog.GetArtistSongs("1", 1, 20, "hot"); },
      [&] { return catalog.GetArtistAlbums("1", 1, 20, "hot"); },
      [&] { return rank.List(); },
      [&] { return rank.GetSongs(1, 1, 20); },
      [&] { return playlists.GetTopPlaylists(0, 1, 20, 1); },
      [&] { return playlists.GetTags(); },
  };
  int checks = 0;
  for (const auto& fixture : {
           R"({"status":0,"errcode":123,"message":"fixture denied"})",
           R"({"status":0,"error":"fixture denied","data":{"info":[]}})",
           R"({"status":1,"error_code":0,"errcode":123,"data":{"info":[]}})",
           R"({"status":1,"error_code":null,"data":{"info":[]}})",
           R"({"status":"1junk","data":{"info":[]}})",
           R"({"data":{"info":[]}})",
           R"({"status":null,"data":{"info":[]}})",
           R"({"status":1,"data":null})",
           R"({"status":1,"data":[]})",
           R"([{"status":1,"data":{"info":[]}}])",
           R"([])", R"(null)", R"(not json)"}) {
    body = fixture;
    for (const auto& method : methods) {
      const auto result = method();
      CHECK(result["status"] == 0);
      CHECK(result.contains("error_code"));
      CHECK(result["error"].is_string() && !result["error"].get<std::string>().empty());
      ++checks;
    }
  }
  // Explicit empty pages remain successful; failures above must never be cached as emptiness.
  body = R"({"status":"1","error_code":"0","errcode":0,"data":{"info":[],"total":"0"}})";
  for (std::size_t i : {1, 3, 4, 5, 6, 7}) {
    const auto result = methods[i]();
    CHECK(result["status"] == 1);
    CHECK(result["data"]["info"].empty());
    CHECK(result["data"]["total"] == 0);
    ++checks;
  }
  for (const auto& fixture : {
           R"({"status":1,"data":{}})",
           R"({"status":1,"data":{"info":null}})",
           R"({"status":1,"data":{"info":{}}})",
           R"({"status":1,"data":{"info":[null,[],"bad"]}})"}) {
    body = fixture;
    for (std::size_t i : {1, 3, 4, 5, 6, 7}) {
      CHECK(methods[i]()["status"] == 0);
      ++checks;
    }
  }
  // Optional corrupt metadata in one row does not discard the valid song or throw HTTP 500.
  body = R"({"status":1,"data":{"info":[null,{"hash":"song","filename":"Artist - Title","duration":2147483647,"trans_param":null,"album_sizable_cover":"https://example.test/cover"}],"total":null}})";
  for (std::size_t i : {1, 3, 6}) {
    const auto result = methods[i]();
    CHECK(result["status"] == 1);
    CHECK(result["data"]["info"].size() == 1);
    const auto& song = result["data"]["info"][0];
    if (i == 6) CHECK(song["audio_info"]["duration"] == 2147483647000LL);
    else {
      CHECK(song["cover"] == "https://example.test/cover");
      CHECK(song["timelen"] == 2147483647000LL);
    }
    ++checks;
  }
  body = R"({"status":1,"data":{"info":[null,{"specialid":1,"specialname":"Fixture","playcount":null,"trans_param":null}],"total":{}}})";
  const auto top = playlists.GetTopPlaylists(0, 1, 20, 1);
  CHECK(top["status"] == 1 && top["data"]["info"].size() == 1);
  ++checks;
  body = R"({"status":1,"data":{"albumid":"1","albumname":null,"songcount":null}})";
  CHECK(catalog.GetAlbumDetail("1")["status"] == 1);
  ++checks;
  body = R"( <!--KG_TAG_RES_START-->{"status":1,"data":{"info":[],"total":0}}<!--KG_TAG_RES_END--> )";
  CHECK(catalog.GetAlbumSongs("1", 1, 20)["status"] == 1);
  CHECK(playlists.GetTopPlaylists(0, 1, 20, 1)["status"] == 1);
  checks += 2;
  std::cout << "Discovery response contracts: " << checks << " checks passed\n";
}

// EchoPlaylistContractTest — tracks output shape, id/listid/global_collection_id, user playlist normalization.
// Extracted from basic_contract_tests.cpp (lines 3415-3478) for independent build.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/PlaylistService.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
  std::cout << "[PlaylistContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── GetTracks output shape contract ─────────────────────────────────
  std::cout << "[PlaylistContract] Testing GetTracks output shape..." << std::endl;
  {
    auto mockGet = [](const std::string&,
                      const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      return {200, R"({"status":1,"data":{"info":[{"hash":"h1","filename":"A - Song","duration":240,"album_audio_id":1,"audio_id":2,"album_id":3,"privilege":10,"pay_type":3}],"total":1}})", ""};
    };

    echo::core::PlaylistService svc(mockGet);

    const auto tracks = svc.GetTracks("1", 1, 10);
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

    std::cout << "  [ok] GetTracks output shape contract" << std::endl;
  }

  // ── GetUserPlaylists output shape + id/listid/global_collection_id ───
  std::cout << "[PlaylistContract] Testing GetUserPlaylists output shape..." << std::endl;
  {
    auto mockPost = [](const std::string&,
                       const std::string&,
                       const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
      return {200, R"({"errcode":0,"data":{"lists":[{"global_collection_id":"c_1","listid":"1","listname":"P1","songcount":5,"img":"img.jpg"}],"total":1}})", ""};
    };

    echo::core::PlaylistService svc(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, "{}", ""};
        },
        mockPost);

    const auto userLists = svc.GetUserPlaylists("42", "tok", 1, 30);
    assert(userLists.contains("status"));
    assert(userLists["status"] == 1);
    assert(userLists.contains("data"));
    assert(userLists["data"].contains("list"));
    assert(userLists["data"]["list"].is_array());
    assert(userLists["data"]["list"].size() == 1);
    const auto& pl = userLists["data"]["list"][0];
    // id/listid/global_collection_id must all be present and correct
    assert(pl.contains("id"));
    assert(pl.contains("global_collection_id"));
    assert(pl.contains("listid"));
    assert(pl.contains("name"));
    assert(pl.contains("songcount"));
    assert(pl.contains("img"));
    assert(pl["id"] == "c_1");
    assert(pl["listid"] == "1");
    assert(pl["global_collection_id"] == "c_1");

    std::cout << "  [ok] GetUserPlaylists output shape + id contract" << std::endl;
  }

  // ── Empty/invalid playlist id returns empty result ────────────────────
  std::cout << "[PlaylistContract] Testing empty id edge cases..." << std::endl;
  {
    echo::core::PlaylistService svc;
    const auto empty0 = svc.GetTracks("", 1, 10);
    assert(empty0["status"] == 1);
    assert(empty0["data"]["songs"].is_array());
    assert(empty0["data"]["songs"].empty());

    const auto empty1 = svc.GetTracks("0", 1, 10);
    assert(empty1["data"]["songs"].empty());

    const auto empty2 = svc.GetTracks("null", 1, 10);
    assert(empty2["data"]["songs"].empty());

    std::cout << "  [ok] Empty/0/null id returns empty tracks" << std::endl;
  }

  std::cout << "[PlaylistContract] All tests passed!" << std::endl;
  return 0;
}

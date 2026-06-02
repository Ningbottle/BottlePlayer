// EchoSongUrlContractTest — v5/v6 output shape, quality selection, preview/paid error, hash normalization.
// Extracted from basic_contract_tests.cpp (lines 3344-3413) for independent build.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/SongUrlService.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
  std::cout << "[SongUrlContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── Resolve public Interface shape contract ─────────────────────────
  // Pins the normalized output shape so P3 (SongUrlService refactor)
  // has a contract to validate against.
  std::cout << "[SongUrlContract] Testing Resolve output shape..." << std::endl;
  {
    echo::core::SongUrlService svc([](
        const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return echo::core::HttpResult{
          200,
          R"({"status":1,"hash":"ABC123","url":"http://cdn.example/abc.flac","backup_url":["http://cdn.example/bak.flac"],"fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","albumid":966846,"album_audio_id":32100650,"audio_id":20505418,"timeLength":269000,"bitRate":320,"extName":"flac","privilege":10,"pay_type":3})",
          ""};
    });

    const auto result = svc.Resolve("ABC123", "", "");
    // Top-level shape
    assert(result.contains("status"));
    assert(result.contains("url"));
    assert(result.contains("data"));
    // data sub-shape: play_url, backup_url, hash, metadata
    assert(result["data"].contains("play_url"));
    assert(result["data"]["play_url"] == "http://cdn.example/abc.flac");
    assert(result["data"].contains("backup_url"));
    assert(result["data"]["backup_url"].is_array());
    assert(result["data"].contains("hash"));
    assert(result["data"].contains("song_name"));
    assert(result["data"].contains("singer_name"));
    assert(result["data"].contains("time_length"));
    assert(result["data"].contains("bit_rate"));
    assert(result["data"].contains("ext_name"));
    assert(result["data"].contains("privilege"));
    assert(result["data"].contains("pay_type"));
    assert(result["data"].contains("album_audio_id"));
    assert(result["data"].contains("audio_id"));
    assert(result["data"].contains("album_id"));

    std::cout << "  [ok] Resolve output shape contract" << std::endl;
  }

  // ── ResolveV6PrivUrl output shape ────────────────────────────────────
  std::cout << "[SongUrlContract] Testing ResolveV6PrivUrl output shape..." << std::endl;
  {
    // MUST inject POST mock — ResolveV6PrivUrl uses POST, not GET.
    // Without mock, falls to real HttpClient and test becomes unreliable.
    echo::core::SongUrlService svc(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected GET in v6 test"};
        },
        [](const std::string&,
           const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":[{"url":"http://cdn.example/vip-320.mp3","info":{"bitrate":320,"filesize":5000,"extname":"mp3","fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","timeLength":240000}},{"url":"http://cdn.example/vip-128.mp3","info":{"bitrate":128,"filesize":2000,"extname":"mp3","fileName":"歌手 - 歌名","songName":"歌名","singerName":"歌手","timeLength":240000}}]})",
              ""};
        });

    echo::core::DeviceInfo device;
    device.dfid = "v6-dfid";
    device.guid = "v6-guid";
    const auto v6 = svc.ResolveV6PrivUrl("VIPHASH", "123", "42", "tok", "vipTok", 3, device);
    // Strong assertions: status must be 1, not just "contains status"
    assert(v6["status"] == 1);
    assert(v6["url"] == "http://cdn.example/vip-320.mp3");
    assert(v6["play_url"] == "http://cdn.example/vip-320.mp3");
    assert(v6.contains("data"));
    assert(v6["data"]["play_url"] == "http://cdn.example/vip-320.mp3");
    assert(v6["data"]["hash"] == "viphash");  // v6 normalizes to lowercase
    assert(v6["data"]["quality"] == "320");   // highest bitrate selected
    assert(v6["data"]["available_qualities"].size() == 2);

    std::cout << "  [ok] ResolveV6PrivUrl output shape contract (POST mock, strong assertions)" << std::endl;
  }

  // ── V6 quality selection: requested quality must be selected ──────────
  std::cout << "[SongUrlContract] Testing V6 quality selection..." << std::endl;
  {
    echo::core::SongUrlService svc(
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

    echo::core::DeviceInfo device;
    device.dfid = "q-dfid";
    device.guid = "q-guid";
    const auto result128 = svc.Resolve(
        "ABC123", "", "32100650", "128", "", "42", "tok", device);
    assert(result128["status"] == 1);
    assert(result128["url"] == "http://cdn.example/song-128.mp3");
    assert(result128["play_url"] == "http://cdn.example/song-128.mp3");
    assert(result128["data"]["quality"] == "128");
    assert(result128["data"]["available_qualities"].size() == 2);

    std::cout << "  [ok] V6 quality selection contract" << std::endl;
  }

  // ── Empty hash must return error ─────────────────────────────────────
  std::cout << "[SongUrlContract] Testing empty hash error..." << std::endl;
  {
    echo::core::SongUrlService svc;
    const auto emptyResult = svc.Resolve("", "", "");
    assert(emptyResult["status"] == 0);
    assert(emptyResult.contains("error"));
    assert(!emptyResult["error"].get<std::string>().empty());

    std::cout << "  [ok] Empty hash returns error" << std::endl;
  }

  // ── Hash normalization: case-insensitive + trimmed ───────────────────
  std::cout << "[SongUrlContract] Testing hash normalization..." << std::endl;
  {
    std::string capturedUrl;
    echo::core::SongUrlService svc([&capturedUrl](
        const std::string& url,
        const std::unordered_map<std::string, std::string>&) {
      capturedUrl = url;
      return echo::core::HttpResult{
          200,
          R"({"status":1,"url":"http://cdn.example/abc.mp3","backup_url":[]})",
          ""};
    });

    svc.Resolve("  ABCdef123  ", "", "");
    // The v5 URL should contain the lowercased, trimmed hash
    assert(capturedUrl.find("abcdef123") != std::string::npos);

    std::cout << "  [ok] Hash normalization contract" << std::endl;
  }

  std::cout << "[SongUrlContract] All tests passed!" << std::endl;
  return 0;
}

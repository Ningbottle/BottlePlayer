// EchoHomeContractTest — HomeService request parameter snapshot.
// Pins the current appid=1014/clientver=20000 behavior for GetBanners/GetEverydayRecommend,
// and appid=3116 for GetImagesAudio.  This contract must be consulted before any P2-3d changes.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/HomeService.h"
#include "echo/core/RadioService.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
  std::cout << "[HomeContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── GetBanners request params snapshot ───────────────────────────────
  {
    echo::core::HomeService service([](const std::string& url, const std::string& body,
        const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
      assert(url.find("/musicadservice/v1/mobile_newalbum_sp") != std::string::npos);
      assert(url.find("signature=") != std::string::npos);
      const auto json = nlohmann::json::parse(body);
      assert(json.at("apiver") == 20 && json.at("page") == 1 && json.at("pagesize") == 100);
      assert(json.at("withpriv") == 1 && json.at("token") == "fixture-token");
      assert(headers.at("Content-Type") == "application/json");
      return {200, R"({"status":1,"data":{"albums":[{"album_id":17,"album_name":"Independent album"}]}})", ""};
    });
    const auto result = service.GetRecommendedAlbums(0, 999, {}, "fixture-token");
    assert(result.at("data").at("albums").size() == 1);
  }
  // CRITICAL: pins the CURRENT behavior where BaseParams hardcodes
  // appid=1014/clientver=20000.  P2-3d must consult this test before changing.
  std::cout << "[HomeContract] Testing GetBanners request params..." << std::endl;
  {
    std::string capturedUrl;
    std::string capturedBody;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::HomeService svc(
        [&capturedUrl, &capturedBody, &capturedHeaders](
            const std::string& url,
            const std::string& body,
            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
          capturedUrl = url;
          capturedBody = body;
          capturedHeaders = headers;
          return {200, R"({"status":1,"data":{"list":[]}})" , ""};
        });
  
    svc.GetBanners("42", "tok");
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("ads.gateway/v3/listen_banner") != std::string::npos);
    // Pin: GetBanners currently uses appid=1014, clientver=20000 (NOT Concept profile)
    assert(capturedUrl.find("appid=1014") != std::string::npos);
    assert(capturedUrl.find("clientver=20000") != std::string::npos);
    // Must have signature
    assert(capturedUrl.find("signature=") != std::string::npos);
    // Body must include userid
    assert(capturedBody.find("\"userid\"") != std::string::npos);
    // Headers: Android UA + JSON content type
    assert(capturedHeaders.count("User-Agent"));
    assert(capturedHeaders.at("Content-Type") == "application/json");
  
    std::cout << "  [ok] GetBanners: appid=1014, clientver=20000, headers pinned" << std::endl;
  }

  // ── GetEverydayRecommend request params snapshot ──────────────────────
  std::cout << "[HomeContract] Testing GetEverydayRecommend request params..." << std::endl;
  {
    std::string capturedUrl;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::HomeService svc(
        [&capturedUrl, &capturedHeaders](
            const std::string& url,
            const std::string& body,
            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
          capturedUrl = url;
          capturedHeaders = headers;
          return {200, R"({"status":1,"data":{}})" , ""};
        });
  
    svc.GetEverydayRecommend("42", "tok");
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("everyday_song_recommend") != std::string::npos);
    // Pin: same BaseParams as GetBanners
    assert(capturedUrl.find("appid=1014") != std::string::npos);
    assert(capturedUrl.find("clientver=20000") != std::string::npos);
    // platform=ios is a known quirk of this endpoint
    assert(capturedUrl.find("platform=ios") != std::string::npos);
    assert(capturedUrl.find("signature=") != std::string::npos);
    // CRITICAL: x-router header routes to correct backend; P2 migration must not drop it
    assert(capturedHeaders.count("x-router"));
    assert(capturedHeaders.at("x-router") == "everydayrec.service.kugou.com");
    assert(capturedHeaders.count("User-Agent"));
  
    std::cout << "  [ok] GetEverydayRecommend: appid=1014, clientver=20000, platform=ios, x-router pinned" << std::endl;
  }

  // ── GetImagesAudio request params snapshot ────────────────────────────
  // This endpoint DOES use GetKuGouProfile(Concept) → appid=3116.
  std::cout << "[HomeContract] Testing GetImagesAudio request params..." << std::endl;
  {
    std::string capturedUrl;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::HomeService svc(
        [&capturedUrl](
            const std::string& url,
            const std::string&,
            const std::unordered_map<std::string, std::string>&) -> echo::core::HttpResult {
          capturedUrl = url;
          return {200, R"({"status":1,"data":[]})" , ""};
        },
        [&capturedUrl, &capturedHeaders](
            const std::string& url,
            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
          capturedUrl = url;
          capturedHeaders = headers;
          return {200, R"({"status":1,"data":[]})" , ""};
        });

    svc.GetImagesAudio("abc123");
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("author_image/audio") != std::string::npos);
    // Pin: GetImagesAudio uses Concept profile → appid=3116
    assert(capturedUrl.find("appid=3116") != std::string::npos);
    assert(capturedUrl.find("clientver=11440") != std::string::npos);
    assert(capturedUrl.find("signature=") != std::string::npos);
    // Headers: Android UA
    assert(capturedHeaders.count("User-Agent"));
    assert(capturedHeaders.count("Accept"));

    std::cout << "  [ok] GetImagesAudio: appid=3116, clientver=11440, headers (Concept profile, pinned)" << std::endl;
  }

  // ── GetPersonalFm request params snapshot ────────────────────────────
  // This is the continuous KuGou Concept "猜你喜欢 / 私人 FM" feed. Unlike
  // everyday recommendations, it accepts current-play context so the queue can
  // keep extending instead of looping a fixed daily list.
  std::cout << "[HomeContract] Testing GetPersonalFm request params..." << std::endl;
  {
    std::string capturedUrl;
    std::string capturedBody;
    std::unordered_map<std::string, std::string> capturedHeaders;
    echo::core::RadioService svc(
        [&capturedUrl, &capturedBody, &capturedHeaders](
            const std::string& url,
            const std::string& body,
            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
          capturedUrl = url;
          capturedBody = body;
          capturedHeaders = headers;
          return {200, R"({"status":1,"data":{"song_list":[]}})" , ""};
        });

    svc.Recommend({.userId = "42", .token = "tok", .hash = "HASH1", .songId = "12345", .playedSeconds = 61, .remainingSongs = 2, .overplay = true});
    assert(!capturedUrl.empty());
    assert(capturedUrl.find("/v2/personal_recommend") != std::string::npos);
    assert(capturedUrl.find("appid=3116") != std::string::npos);
    assert(capturedUrl.find("clientver=11440") != std::string::npos);
    assert(capturedUrl.find("signature=") != std::string::npos);
    assert(capturedHeaders.count("x-router"));
    assert(capturedHeaders.at("x-router") == "persnfm.service.kugou.com");
    assert(capturedHeaders.count("Content-Type"));
    assert(capturedHeaders.at("Content-Type") == "application/json");
    assert(capturedBody.find("\"action\":\"play\"") != std::string::npos);
    assert(capturedBody.find("\"hash\":\"HASH1\"") != std::string::npos);
    assert(capturedBody.find("\"songid\":\"12345\"") != std::string::npos);
    assert(capturedBody.find("\"playtime\":61") != std::string::npos);
    assert(capturedBody.find("\"remain_songcnt\":2") != std::string::npos);
    assert(capturedBody.find("\"is_overplay\":1") != std::string::npos);
    assert(capturedBody.find("\"userid\":\"42\"") != std::string::npos);
    assert(capturedBody.find("\"token\":\"tok\"") != std::string::npos);
    assert(capturedBody.find("\"key\"") != std::string::npos);

    std::cout << "  [ok] GetPersonalFm: /v2/personal_recommend, context body, x-router pinned" << std::endl;
  }

  // Radio validates the observed action and bounded play context before transport.
  {
    int calls = 0;
    nlohmann::json body;
    echo::core::RadioService radio([&](const auto&, const auto& data, const auto&) -> echo::core::HttpResult {
      ++calls; body = nlohmann::json::parse(data);
      return {200, R"({"status":1,"data":{"song_list":[]}})", ""};
    });
    radio.Recommend({.playedSeconds = 99, .remainingSongs = -5, .songPoolId = -1});
    assert(calls == 1);
    assert(!body.contains("userid") && !body.contains("token") && !body.contains("playtime"));
    assert(body.at("remain_songcnt") == 0 && body.at("song_pool_id") == 0);
    radio.Recommend({.hash = "HASH", .playedSeconds = 1000000, .remainingSongs = 999});
    assert(body.at("playtime") == 86400 && body.at("remain_songcnt") == 100);
    const auto invalid = radio.Recommend({.action = "invented-like-action"});
    assert(invalid.at("status") == 0 && calls == 2);
    echo::core::RadioService malformed([](const auto&, const auto&, const auto&) -> echo::core::HttpResult { return {200, "not json", ""}; });
    assert(malformed.Recommend({}).at("error") == "invalid_radio_response");
  }

  std::cout << "[HomeContract] All tests passed!" << std::endl;
  return 0;
}

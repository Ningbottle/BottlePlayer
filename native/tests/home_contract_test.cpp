// EchoHomeContractTest — HomeService request parameter snapshot.
// Pins the current appid=1014/clientver=20000 behavior for GetBanners/GetEverydayRecommend,
// and appid=3116 for GetImagesAudio.  This contract must be consulted before any P2-3d changes.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/HomeService.h"
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
    echo::core::HomeService svc(
        [&capturedUrl, &capturedBody, &capturedHeaders](
            const std::string& url,
            const std::string& body,
            const std::unordered_map<std::string, std::string>& headers) -> echo::core::HttpResult {
          capturedUrl = url;
          capturedBody = body;
          capturedHeaders = headers;
          return {200, R"({"status":1,"data":{"song_list":[]}})" , ""};
        });

    svc.GetPersonalFm("42", "tok", "HASH1", "12345", 61, 2, true);
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

  std::cout << "[HomeContract] All tests passed!" << std::endl;
  return 0;
}

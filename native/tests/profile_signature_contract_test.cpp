// EchoProfileSignatureContractTest — KuGouProfile parameters, salt kind selection, signature consistency.
// Extracted from basic_contract_tests.cpp (lines 137-220) for independent build.

#include <cassert>
#include <iostream>
#include <string>

#include "echo/core/KuGouProfile.h"
#include "echo/core/KuGouAndroidRequest.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
  std::cout << "[ProfileSignatureContract] started" << std::endl;
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── KuGouProfile parameter contract ─────────────────────────────────
  // Concept edition must always return the documented parameters.
  std::cout << "[ProfileSignatureContract] Testing GetKuGouProfile(Concept)..." << std::endl;
  {
    const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    assert(profile.appid == "3116");
    assert(profile.clientver == "11440");
    assert(profile.busiType == "concept");
    assert(profile.saltKind == echo::core::KuGouSaltKind::Lite);

    std::cout << "  [ok] Concept profile: appid=3116, clientver=11440, busi_type=concept, salt=Lite" << std::endl;
  }

  // ── Standard profile for comparison ──────────────────────────────────
  std::cout << "[ProfileSignatureContract] Testing GetKuGouProfile(Standard)..." << std::endl;
  {
    const auto profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Standard);
    assert(profile.appid == "1005");
    assert(profile.clientver == "20489");
    assert(profile.saltKind == echo::core::KuGouSaltKind::Standard);

    std::cout << "  [ok] Standard profile: appid=1005, clientver=20489, salt=Standard" << std::endl;
  }

  // ── Named constant contract ──────────────────────────────────────────
  std::cout << "[ProfileSignatureContract] Testing named constants..." << std::endl;
  {
    assert(std::string(echo::core::QrLoginAppId) == "1001");
    assert(std::string(echo::core::V5UrlClientver) == "11430");

    std::cout << "  [ok] QrLoginAppId=1001, V5UrlClientver=11430" << std::endl;
  }

  // ── BuildSignedUrl contract ─────────────────────────────────────────
  std::cout << "[ProfileSignatureContract] Testing BuildSignedUrl..." << std::endl;
  {
    echo::core::KuGouAndroidRequest req;
    req.endpoint = "https://gateway.kugou.com/v3/get_my_info";
    req.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    req.includeSongUrlKey = true;
    req.params["hash"] = "abc123";
    req.params["quality"] = "128";
    req.params["clienttime"] = "1700000000";
    req.device.dfid = "dfid-test";
    req.device.guid = "guid-test";

    const auto url = echo::core::BuildSignedUrl(req);
    assert(!url.empty());
    assert(url.find("https://gateway.kugou.com/v3/get_my_info?") == 0);
    assert(url.find("hash=abc123") != std::string::npos);
    assert(url.find("quality=128") != std::string::npos);
    assert(url.find("appid=3116") != std::string::npos);
    assert(url.find("clientver=11440") != std::string::npos);
    assert(url.find("clienttime=1700000000") != std::string::npos);
    assert(url.find("dfid=dfid-test") != std::string::npos);
    assert(url.find("signature=") != std::string::npos);
    assert(url.find("key=") != std::string::npos);

    std::cout << "  [ok] BuildSignedUrl includes all required params + signature + key" << std::endl;
  }

  // ── BuildAndroidHeaders contract ────────────────────────────────────
  std::cout << "[ProfileSignatureContract] Testing BuildAndroidHeaders..." << std::endl;
  {
    echo::core::KuGouAndroidRequest req;
    req.endpoint = "https://gateway.kugou.com/v3/get_my_info";
    req.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    req.params["clienttime"] = "1700000000";
    req.device.dfid = "dfid-test";
    req.device.guid = "guid-test";

    const auto headers = echo::core::BuildAndroidHeaders(req);
    assert(headers.count("dfid") && headers.at("dfid") == "dfid-test");
    assert(headers.count("mid"));
    assert(headers.count("clienttime") && headers.at("clienttime") == "1700000000");
    assert(headers.at("kg-rf") == "B9EDA08A64250DEFFBCADDEE00F8F25F");
    assert(headers.at("Accept") == "application/json");

    std::cout << "  [ok] BuildAndroidHeaders includes all required fingerprint headers" << std::endl;
  }

  // ── /v5/url clientver override contract ──────────────────────────────
  // /v5/url MUST use V5UrlClientver=11430, NOT the global 11440.
  // This test pins the override so P2 migration cannot silently drop it.
  std::cout << "[ProfileSignatureContract] Testing /v5/url clientver override..." << std::endl;
  {
    echo::core::KuGouAndroidRequest v5req;
    v5req.endpoint = "https://gateway.kugou.com/v5/url";
    auto v5profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    // Caller must override clientver to V5UrlClientver for /v5/url
    v5profile.clientver = echo::core::V5UrlClientver;
    v5req.profile = v5profile;
    v5req.params["hash"] = "test";
    v5req.params["clienttime"] = "1700000000";
    v5req.device.dfid = "v5-dfid";

    const auto v5url = echo::core::BuildSignedUrl(v5req);
    assert(v5url.find("clientver=11430") != std::string::npos);
    assert(v5url.find("clientver=11440") == std::string::npos);  // must NOT be global default

    std::cout << "  [ok] /v5/url uses clientver=11430 (V5UrlClientver override), not 11440" << std::endl;
  }

  // ── Edge: no hash → no key parameter ────────────────────────────────
  std::cout << "[ProfileSignatureContract] Testing edge cases..." << std::endl;
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

    std::cout << "  [ok] No hash → no key parameter generated" << std::endl;
  }

  // ── Edge: empty device → mid="0", dfid="-" ─────────────────────────
  {
    echo::core::KuGouAndroidRequest emptyDeviceReq;
    emptyDeviceReq.endpoint = "https://gateway.kugou.com/v5/url";
    emptyDeviceReq.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    emptyDeviceReq.includeSongUrlKey = true;
    emptyDeviceReq.params["hash"] = "xyz";
    const auto url = echo::core::BuildSignedUrl(emptyDeviceReq);
    assert(url.find("mid=0") != std::string::npos);
    assert(url.find("dfid=-") != std::string::npos);

    std::cout << "  [ok] Empty device → mid=0, dfid=-" << std::endl;
  }

  // ── Signature determinism: same inputs → same signature ─────────────
  std::cout << "[ProfileSignatureContract] Testing signature determinism..." << std::endl;
  {
    echo::core::KuGouAndroidRequest req1, req2;
    req1.endpoint = req2.endpoint = "https://gateway.kugou.com/v3/test";
    req1.profile = req2.profile = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
    req1.params = req2.params = {{"userid", "42"}, {"clienttime", "1700000000"}};
    req1.device = req2.device;
    req1.device.dfid = req2.device.dfid = "det-dfid";
    req1.device.guid = req2.device.guid = "det-guid";

    const auto url1 = echo::core::BuildSignedUrl(req1);
    const auto url2 = echo::core::BuildSignedUrl(req2);
    assert(url1 == url2);

    std::cout << "  [ok] Same inputs produce identical signatures" << std::endl;
  }

  std::cout << "[ProfileSignatureContract] All tests passed!" << std::endl;
  return 0;
}

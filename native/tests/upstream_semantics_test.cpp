// EchoUpstreamSemanticsTest — characterization of the three upstream "success"
// judgments, observed through public service entry points only.
//
// The repo has three coexisting upstream success semantics (plan §3.1):
//   1. LyricService::BusinessSuccess + UpstreamErrorCode (string status,
//      ZeroBusinessCode, first non-zero error code wins)
//   2. mobile_catalog::Success (integer status, field-present-but-unparseable
//      fails, any non-zero fails)
//   3. SongUrlService errcode-first ReadInt(errcode, ReadInt(error_code))
// This test records the CURRENT classification for the response shapes that
// can change error classification. It pins existing behavior on main; batch 2
// may only change an assertion after the corresponding fix is approved.

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "echo/core/CatalogService.h"
#include "echo/core/LyricService.h"
#include "echo/core/RankService.h"
#include "echo/core/SongUrlService.h"
#include "echo/core/HttpClient.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

using nlohmann::json;

echo::core::HttpResult Body(std::string body) {
  return echo::core::HttpResult{200, std::move(body), ""};
}

std::string Dump(const json& value) { return value.dump(); }

// ── mobile_catalog::Success (via CatalogService::GetAlbumSongs) ──────────
void CharacterizeMobileCatalogSuccess() {
  // Success: status=1, no error fields.
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"data":{"info":[{"hash":"h1","filename":"a - b"}],"total":1}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 1);
    assert(result["data"]["info"].size() == 1);
  }
  // Divergence vs LyricService: errcode:"" (present, unparseable) FAILS.
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"","data":{"info":[]}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 0);
    assert(result.value("error", "").find("rejected") != std::string::npos);
  }
  // errcode:"x" (present, unparseable) fails.
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"x","data":{"info":[]}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 0);
  }
  // Divergence vs SongUrl/ReadKuGouErrorCode: error_code=0 does NOT rescue
  // errcode=20018 — any non-zero error field fails.
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"error_code":0,"errcode":20018,"data":{"info":[]}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 0);
  }
  // String status "1" is accepted (Integer parses numeric strings).
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":"1","data":{"info":[{"hash":"h2","filename":"c - d"}],"total":1}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 1);
  }
  // String errcode "0" is accepted as zero.
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"0","data":{"info":[{"hash":"h3","filename":"e - f"}],"total":1}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 1);
  }
  // Float status is unparseable → failure (fallback wins).
  {
    echo::core::CatalogService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1.0,"data":{"info":[]}})");
    });
    const auto result = svc.GetAlbumSongs("42", 1, 30);
    assert(result.value("status", 0) == 0);
  }
  std::cout << "  [ok] mobile_catalog::Success matrix (via CatalogService)" << std::endl;
}

// Same judgment, second consumer (RankService::GetSongs uses json::parse
// without the HTML-comment unwrap). Shapes kept identical on purpose.
void CharacterizeMobileCatalogSuccessViaRank() {
  {
    echo::core::RankService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"","data":{"info":[{"hash":"r1"}],"total":1}})");
    });
    const auto result = svc.GetSongs(888, 1, 30);
    assert(result.value("status", 0) == 0);
    assert(result.value("error", "").find("rejected") != std::string::npos);
  }
  {
    echo::core::RankService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"error_code":0,"errcode":20018,"data":{"info":[{"hash":"r2"}],"total":1}})");
    });
    const auto result = svc.GetSongs(888, 1, 30);
    assert(result.value("status", 0) == 0);
  }
  std::cout << "  [ok] mobile_catalog::Success matrix (via RankService)" << std::endl;
}

// ── LyricService::BusinessSuccess + UpstreamErrorCode ────────────────────
void CharacterizeLyricBusinessSuccess() {
  // Success: string/integer status "1", no error fields → status 1,
  // no error_code copied.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"candidates":[{"id":"i1","accesskey":"k1"}]})");
    });
    const auto result = svc.Search("lyrichash-a");
    assert(result.value("status", 0) == 1);
    assert(!result.contains("error_code"));
  }
  // Divergence vs mobile_catalog: errcode:"" is ZeroBusinessCode → SUCCESS.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"","candidates":[{"id":"i2","accesskey":"k2"}]})");
    });
    const auto result = svc.Search("lyrichash-b");
    assert(result.value("status", 0) == 1);
    assert(result.contains("error_code") && result["error_code"] == "");
  }
  // errcode:"x" fails; UpstreamErrorCode surfaces the raw value.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"errcode":"x","candidates":[]})");
    });
    const auto result = svc.Search("lyrichash-c");
    assert(result.value("status", 0) == 0);
    assert(result.value("error_code", "") == "x");
  }
  // Divergence: first non-zero wins → 20018 surfaced even though
  // error_code=0; classification FAILS (vs ReadKuGouErrorCode which
  // would prefer error_code=0 → no error).
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1,"error_code":0,"errcode":20018,"candidates":[]})");
    });
    const auto result = svc.Search("lyrichash-d");
    assert(result.value("status", 0) == 0);
    assert(result.value("error_code", 0) == 20018);
  }
  // Legacy search endpoint: status "200" maps to success and the output
  // status is 200.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":"200","candidates":[{"id":"i3","accesskey":"k3"}]})");
    });
    const auto result = svc.Search("lyrichash-e");
    assert(result.value("status", 0) == 200);
  }
  // errcode=200 is only tolerated on the search path with status "200";
  // the output still carries error_code=200.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":"200","errcode":200,"candidates":[{"id":"i4","accesskey":"k4"}]})");
    });
    const auto result = svc.Search("lyrichash-f");
    assert(result.value("status", 0) == 200);
    assert(result.value("error_code", 0) == 200);
  }
  // Missing status fails.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"candidates":[]})");
    });
    const auto result = svc.Search("lyrichash-g");
    assert(result.value("status", 0) == 0);
  }
  // Float status: Lyric's ReadString does not handle floats → "" → failure.
  {
    echo::core::LyricService svc([](const std::string&,
        const std::unordered_map<std::string, std::string>&) {
      return Body(R"({"status":1.0,"candidates":[]})");
    });
    const auto result = svc.Search("lyrichash-h");
    assert(result.value("status", 0) == 0);
  }
  std::cout << "  [ok] BusinessSuccess/UpstreamErrorCode matrix (via Search)" << std::endl;
}

// ── SongUrlService errcode-first reading ─────────────────────────────────
void CharacterizeSongUrlErrcodePriority() {
  const auto failingBody = R"({"status":0,"error_code":0,"errcode":20018})";
  int getCalls = 0;
  int postCalls = 0;
  echo::core::SongUrlService svc(
      echo::core::SongUrlHttpGetWithTimeout(
          [&](const std::string&,
              const std::unordered_map<std::string, std::string>&,
              long) {
            ++getCalls;
            return Body(failingBody);
          }),
      echo::core::SongUrlHttpPostWithTimeout(
          [&](const std::string&,
              const std::string&,
              const std::unordered_map<std::string, std::string>&,
              long) {
            ++postCalls;
            return Body(failingBody);
          }),
      [] { return std::chrono::steady_clock::time_point{}; });

  echo::core::DeviceInfo device;
  device.dfid = "semantics-dfid";
  device.mid = "123456789012345678901234567890123456789";
  device.registered = true;
  const auto result = svc.Resolve("semantichash", "1", "1", "128", "", "42",
      "token", device, "vip-token", 3);

  // errcode is read FIRST: 20018 is attributed even though error_code=0.
  // Under error_code-first semantics (ReadKuGouErrorCode) this response
  // would classify as "no error" and "20018" would never surface.
  assert(result.value("status", 1) == 0);
  assert(Dump(result).find("20018") != std::string::npos);
  assert(getCalls >= 1);
  assert(postCalls >= 1);
  std::cout << "  [ok] SongUrl errcode-first attribution (via Resolve)" << std::endl;
}

}  // namespace

int main() {
  std::cout << "[UpstreamSemantics] started" << std::endl;
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_error_mode(_OUT_TO_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  CharacterizeMobileCatalogSuccess();
  CharacterizeMobileCatalogSuccessViaRank();
  CharacterizeLyricBusinessSuccess();
  CharacterizeSongUrlErrcodePriority();
  std::cout << "[UpstreamSemantics] all characterization cases passed" << std::endl;
  return 0;
}

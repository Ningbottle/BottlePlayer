// EchoUpstreamDialectProbe — read-only live spot check of the response shapes
// the three upstream success dialects actually receive. Manual run only,
// deliberately NOT registered with CTest: it performs real anonymous GETs to
// the same public endpoints the services use (no account, no writes).
//
// Build & run:
//   cmake --build output/native-build --target EchoUpstreamDialectProbe
//   output/native-build/EchoUpstreamDialectProbe.exe
//
// The probe only PRINTS the top-level status/error_code/errcode fields it
// observes; it asserts nothing. Findings feed the dialect notes in
// UpstreamFields.h and upstream_semantics_test.cpp.

#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/HttpClient.h"
#include "echo/core/UpstreamFields.h"

namespace {

void Probe(const char* label, const std::string& url) {
  std::cout << "== " << label << " ==" << std::endl;
  echo::core::HttpClient client;
  const auto result = client.Get(url, {{"Accept", "application/json"},
                                          {"User-Agent", "EchoMusicNative/0.1"}},
                                 30000);
  std::cout << "  http=" << result.statusCode
            << " error=\"" << result.error << "\"" << std::endl;
  if (result.body.empty()) {
    std::cout << "  body=<empty>" << std::endl;
    return;
  }
  try {
    const auto body = nlohmann::json::parse(result.body);
    const auto fields = echo::core::upstream::Read(body);
    const auto describe = [](const echo::core::upstream::Field& field) {
      if (!field) return std::string{"absent"};
      return field.raw->is_string() ? ("string:" + field.raw->get<std::string>())
                                    : field.raw->dump();
    };
    std::cout << "  status=" << describe(fields.status)
              << " error_code=" << describe(fields.errorCode)
              << " errcode=" << describe(fields.errcode) << std::endl;
    std::cout << "  body[:240]=" << result.body.substr(0, 240) << std::endl;
  } catch (const std::exception& error) {
    std::cout << "  parse-failed: " << error.what() << std::endl;
  }
}

}  // namespace

int main() {
  std::cout << "[UpstreamDialectProbe] started" << std::endl;
  // mobilecdn dialect (mobile_catalog::Success consumers).
  Probe("rank/list", "http://mobilecdn.kugou.com/api/v3/rank/list?plat=0");
  // LyricService dialect (BusinessSuccess consumers).
  Probe("lyric/search", "http://lyrics.kugou.com/search?ver=1&man=yes&client=pc&hash=0a9d3e60e2cbb0f3d1e1b6a2f4d0e4a1");
  std::cout << "[UpstreamDialectProbe] done" << std::endl;
  return 0;
}

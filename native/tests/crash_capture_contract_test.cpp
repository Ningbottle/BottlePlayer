#include "echo/diagnostics/CrashCapture.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// Stage 0 RED-phase contract test for crash forensics (plan §Stage 0).
//
// These checks encode what the user must be able to do AFTER a crash: read a
// report that names the exact build (hash), the crash directory, and contains
// a stack walk. The GREEN implementation is diagnostics/CrashCapture.cpp
// (MiniDumpWriteDump + SymFromAddr walk); this test only exercises the
// deterministic parts through WriteCrashReportForTest.

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed: " #condition << " at line " << __LINE__       \
                << std::endl;                                                   \
      return 1;                                                                 \
    }                                                                           \
  } while (false)

namespace fs = std::filesystem;

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path crashDir = fs::temp_directory_path() /
                            ("bottlemusic-crash-capture-" + std::to_string(nonce));

  echo::diagnostics::CrashCaptureConfig config;
  config.dump_directory = crashDir.wstring();
  // build_hash left empty on purpose: asserts the CMake-stamped compiled-in
  // identity is used, not a caller-provided value.
  const bool installed = echo::diagnostics::InstallCrashCapture(config);
  CHECK(installed);
  CHECK(fs::exists(crashDir));

  // Installed facility reports its identity.
  const std::string hash = echo::diagnostics::CrashCaptureBuildHash();
  CHECK(!hash.empty());
  CHECK(hash != "unknown");

  // The capture core runs without raising anything and produces a report.
  const std::string reportPath =
      echo::diagnostics::WriteCrashReportForTest("crash-capture-contract-test");
  CHECK(!reportPath.empty());
  CHECK(fs::exists(fs::path(reportPath)));

  // Report names the build hash and the reason; the directory contains the
  // report (and, in live fault handling, the minidump next to it).
  std::ifstream report(reportPath);
  CHECK(report.is_open());
  std::stringstream buffer;
  buffer << report.rdbuf();
  const std::string text = buffer.str();
  CHECK(text.find(hash) != std::string::npos);
  CHECK(text.find("crash-capture-contract-test") != std::string::npos);

  std::error_code ignored;
  fs::remove_all(crashDir, ignored);

  std::cout << "[CrashCaptureContract] report + build hash captured" << std::endl;
  return 0;
}

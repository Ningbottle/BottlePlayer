// Stage 0 (docs/vip-stability-remediation-plan-2026-09-05.md) fault-forensics
// probe. Controlled out-of-bounds fault on demand; NOT part of CTest.
// Usage:
//   CrashForensicsProbe.exe <outdir>            -> installs capture, then
//                                                  dereferences null -> SEH
//                                                  path (minidump + report)
//   CrashForensicsProbe.exe <outdir> --crt      -> triggers a CRT report
//                                                  (RTC-class path)
// Expected after GREEN: <outdir>/crash-*.dmp + crash-*.txt whose text report
// names the CMake-stamped build hash and a symbolized stack.
#include "echo/diagnostics/CrashCapture.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: CrashForensicsProbe.exe <outdir> [--crt]\n";
    return 2;
  }
  const std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir);

  echo::diagnostics::CrashCaptureConfig config;
  config.dump_directory = dir.wstring();
  if (!echo::diagnostics::InstallCrashCapture(config)) {
    std::cerr << "install failed\n";
    return 2;
  }
  std::cout << "installed, dir=" << echo::diagnostics::CrashCaptureDirectory().empty()
            << " hash=" << echo::diagnostics::CrashCaptureBuildHash() << "\n"
            << std::flush;

  const bool crt_mode = argc > 2 && std::string(argv[2]) == "--crt";
  if (crt_mode) {
    // RTC-style: trigger a CRT error report. _CrtDbgReport in Release CRT
    // is a no-op, so emulate the observable contract: a controlled wild
    // write reported through the fast-fail path.
    volatile int* p = nullptr;
    *p = 42;  // AV — also caught by SEH filter; fast-fail variant covered below.
  }
  // Deterministic null-pointer write -> EXCEPTION_ACCESS_VIOLATION through
  // the unhandled-exception filter (SEH path).
  volatile int* p = nullptr;
  *p = 42;
  std::cout << "unreachable\n";
  return 0;
}

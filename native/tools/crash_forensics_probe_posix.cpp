#include "echo/diagnostics/CrashCapture.h"

#include <csignal>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  echo::diagnostics::CrashCaptureConfig config;
  if (argc > 1) {
    const std::string directory(argv[1]);
    config.dump_directory.assign(directory.begin(), directory.end());
  }
  if (!echo::diagnostics::InstallCrashCapture(config)) return 1;
  std::cout << "installed, dir=" << echo::diagnostics::CrashCaptureDirectory().size()
            << " hash=" << echo::diagnostics::CrashCaptureBuildHash() << std::endl;
  std::raise(SIGABRT);
}

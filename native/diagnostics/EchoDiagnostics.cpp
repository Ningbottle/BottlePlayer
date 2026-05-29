#include "echo/diagnostics/EchoDiagnostics.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <iostream>
#endif

#include <sstream>
#include <string>

namespace echo::diagnostics {

void LogDebug(std::string_view tag, std::string_view message) {
  std::ostringstream stream;
  stream << "[" << tag << "] " << message << "\n";
  const std::string line = stream.str();
#if defined(_WIN32)
  OutputDebugStringA(line.c_str());
#else
  std::cerr << line;
#endif
}

}  // namespace echo::diagnostics

#include "echo/diagnostics/ScopedTimer.h"

namespace echo::diagnostics {

Stopwatch Stopwatch::Start() {
  return Stopwatch(Clock::now());
}

Stopwatch::Stopwatch(TimePoint start) : start_(start) {}

long long Stopwatch::ElapsedMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now() - start_)
      .count();
}

}  // namespace echo::diagnostics

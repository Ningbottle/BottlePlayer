#pragma once

#include <chrono>

namespace echo::diagnostics {

class Stopwatch {
 public:
  static Stopwatch Start();

  long long ElapsedMs() const;

 private:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit Stopwatch(TimePoint start);

  TimePoint start_;
};

}  // namespace echo::diagnostics

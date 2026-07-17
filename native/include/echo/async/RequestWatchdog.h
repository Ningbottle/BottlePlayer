#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace echo::async {

// Process-wide deadline watchdog: single worker + min-heap of
// (deadline, claimed, action). Winner of CAS on claimed runs action once.
// Lazy-drop completed entries (claimed already true).
class RequestWatchdog {
 public:
  static RequestWatchdog& Instance();

  // On deadline, if CAS claims, run action().
  void Arm(long timeoutMs, std::shared_ptr<std::atomic_bool> claimed,
           std::function<void()> action);

  RequestWatchdog(const RequestWatchdog&) = delete;
  RequestWatchdog& operator=(const RequestWatchdog&) = delete;

 private:
  RequestWatchdog() = default;
  ~RequestWatchdog();

  struct Entry {
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t seq = 0;
    std::shared_ptr<std::atomic_bool> claimed;
    std::function<void()> action;
  };

  struct Cmp {
    bool operator()(const Entry& a, const Entry& b) const {
      if (a.deadline != b.deadline) return a.deadline > b.deadline;
      return a.seq > b.seq;
    }
  };

  void EnsureWorkerLocked();
  void Loop();

  std::mutex mu_;
  std::condition_variable cv_;
  std::priority_queue<Entry, std::vector<Entry>, Cmp> heap_;
  std::thread worker_;
  bool workerStarted_ = false;
  bool stop_ = false;
  std::atomic<std::uint64_t> nextSeq_{1};
};

}  // namespace echo::async

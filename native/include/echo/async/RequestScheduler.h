#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>

#include "echo/async/TaskScheduler.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/ScopedTimer.h"

namespace echo::async {

enum class RequestKind {
  SongUrl,
  Search,
  Playlist,
  LoginPoll,
  Image,
  Generic,
};

class RequestScheduler {
 public:
  explicit RequestScheduler(std::size_t workerCount = 4);
  ~RequestScheduler();

  RequestScheduler(const RequestScheduler&) = delete;
  RequestScheduler& operator=(const RequestScheduler&) = delete;

  template <class Fn>
  auto Submit(RequestKind kind, Fn fn) -> std::future<std::invoke_result_t<Fn, CancellationToken>>;

  template <class Fn>
  auto SubmitLatest(RequestKind kind, Fn fn) -> std::future<std::invoke_result_t<Fn, CancellationToken>>;

  template <class Fn>
  void SubmitDetached(RequestKind kind, Fn fn);

  template <class Fn>
  void SubmitLatestDetached(RequestKind kind, Fn fn);

  void Cancel(RequestKind kind);
  void Shutdown();

 private:
  struct Job {
    std::function<void()> execute;
    std::shared_ptr<diagnostics::Stopwatch> enqueueStopwatch;
  };

  void WorkerLoop();
  std::shared_ptr<std::atomic_bool> PrepareLatestToken(RequestKind kind, std::uint64_t& outGen);
  void EnqueueJob(Job job);

  std::size_t workerCount_;
  std::vector<std::thread> workers_;
  std::deque<Job> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool shutdown_ = false;
  std::size_t maxQueueSize_;

  static constexpr std::size_t kKindCount = 6;
  std::array<std::atomic<std::uint64_t>, kKindCount> generations_{};
  std::array<std::shared_ptr<std::atomic_bool>, kKindCount> cancelledFlags_{};
  std::mutex kindMutex_;
};

template <class Fn>
auto RequestScheduler::Submit(RequestKind kind, Fn fn)
    -> std::future<std::invoke_result_t<Fn, CancellationToken>> {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  auto promise = std::make_shared<std::promise<ReturnType>>();
  auto future = promise->get_future();
  auto tokenFlag = std::make_shared<std::atomic_bool>(false);
  auto enqueueStopwatch = std::make_shared<diagnostics::Stopwatch>(diagnostics::Stopwatch::Start());

  auto execute = [fn = std::move(fn), promise, tokenFlag, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
        promise->set_value();
      } else {
        promise->set_value(fn(CancellationToken(tokenFlag)));
      }
    } catch (...) {
      promise->set_exception(std::current_exception());
    }

    const auto runMs = runStopwatch.ElapsedMs();
    std::ostringstream log;
    log << "kind=" << static_cast<int>(kind)
        << " queue_wait_ms=" << queueWaitMs
        << " run_ms=" << runMs
        << " canceled=" << (canceled ? 'Y' : 'N');
    ECHO_LOG("RequestScheduler", log.str());
  };

  EnqueueJob({std::move(execute), enqueueStopwatch});
  return future;
}

template <class Fn>
auto RequestScheduler::SubmitLatest(RequestKind kind, Fn fn)
    -> std::future<std::invoke_result_t<Fn, CancellationToken>> {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  auto promise = std::make_shared<std::promise<ReturnType>>();
  auto future = promise->get_future();

  std::uint64_t myGen = 0;
  auto tokenFlag = PrepareLatestToken(kind, myGen);
  auto enqueueStopwatch = std::make_shared<diagnostics::Stopwatch>(diagnostics::Stopwatch::Start());

  auto execute = [fn = std::move(fn), promise, tokenFlag, myGen, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
        promise->set_value();
      } else {
        promise->set_value(fn(CancellationToken(tokenFlag)));
      }
    } catch (...) {
      promise->set_exception(std::current_exception());
    }

    const auto runMs = runStopwatch.ElapsedMs();
    std::ostringstream log;
    log << "kind=" << static_cast<int>(kind)
        << " gen=" << myGen
        << " queue_wait_ms=" << queueWaitMs
        << " run_ms=" << runMs
        << " canceled=" << (canceled ? 'Y' : 'N');
    ECHO_LOG("RequestScheduler", log.str());
  };

  EnqueueJob({std::move(execute), enqueueStopwatch});
  return future;
}

template <class Fn>
void RequestScheduler::SubmitDetached(RequestKind kind, Fn fn) {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  auto tokenFlag = std::make_shared<std::atomic_bool>(false);
  auto enqueueStopwatch = std::make_shared<diagnostics::Stopwatch>(diagnostics::Stopwatch::Start());

  auto execute = [fn = std::move(fn), tokenFlag, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
      } else {
        (void)fn(CancellationToken(tokenFlag));
      }
    } catch (...) {
      // Detached: swallow exception so worker stays alive. Log is still emitted below.
    }

    const auto runMs = runStopwatch.ElapsedMs();
    std::ostringstream log;
    log << "kind=" << static_cast<int>(kind)
        << " queue_wait_ms=" << queueWaitMs
        << " run_ms=" << runMs
        << " canceled=" << (canceled ? 'Y' : 'N');
    ECHO_LOG("RequestScheduler", log.str());
  };

  EnqueueJob({std::move(execute), enqueueStopwatch});
}

template <class Fn>
void RequestScheduler::SubmitLatestDetached(RequestKind kind, Fn fn) {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  std::uint64_t myGen = 0;
  auto tokenFlag = PrepareLatestToken(kind, myGen);
  auto enqueueStopwatch = std::make_shared<diagnostics::Stopwatch>(diagnostics::Stopwatch::Start());

  auto execute = [fn = std::move(fn), tokenFlag, myGen, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
      } else {
        (void)fn(CancellationToken(tokenFlag));
      }
    } catch (...) {
      // Detached: swallow exception so worker stays alive. Log is still emitted below.
    }

    const auto runMs = runStopwatch.ElapsedMs();
    std::ostringstream log;
    log << "kind=" << static_cast<int>(kind)
        << " gen=" << myGen
        << " queue_wait_ms=" << queueWaitMs
        << " run_ms=" << runMs
        << " canceled=" << (canceled ? 'Y' : 'N');
    ECHO_LOG("RequestScheduler", log.str());
  };

  EnqueueJob({std::move(execute), enqueueStopwatch});
}

}  // namespace echo::async

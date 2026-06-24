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
  auto SubmitWithDeadline(RequestKind kind, Fn fn, long deadlineMs)
      -> std::future<std::invoke_result_t<Fn, CancellationToken>>;

  template <class Fn>
  auto SubmitLatest(RequestKind kind, Fn fn) -> std::future<std::invoke_result_t<Fn, CancellationToken>>;

  template <class Fn>
  void SubmitDetached(RequestKind kind, Fn fn);

  template <class Fn>
  void SubmitLatestDetached(RequestKind kind, Fn fn);

  void Cancel(RequestKind kind);
  // Unbounded Shutdown — joins all workers, may block indefinitely on long
  // jobs. Prefer Shutdown(maxWait) for process-exit paths.
  void Shutdown();
  // Bounded Shutdown — sets shutdown_ + cancels active tokens, then waits
  // up to maxWait for workers to finish. Workers that don't finish in time
  // are detached; the process is expected to be exiting so resource leaks
  // are acceptable. Returns the number of workers that had to be detached.
  std::size_t Shutdown(std::chrono::milliseconds maxWait);

 private:
  struct Job {
    std::function<void()> execute;
    std::shared_ptr<diagnostics::Stopwatch> enqueueStopwatch;
  };

  void WorkerLoop();
  std::shared_ptr<std::atomic_bool> PrepareLatestToken(RequestKind kind, std::uint64_t& outGen);
  bool EnqueueJob(Job job);

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

  if (!EnqueueJob({std::move(execute), enqueueStopwatch})) {
    try {
      promise->set_exception(
          std::make_exception_ptr(std::runtime_error("queue_full")));
    } catch (...) {}
  }
  return future;
}

template <class Fn>
auto RequestScheduler::SubmitWithDeadline(RequestKind kind, Fn fn, long deadlineMs)
    -> std::future<std::invoke_result_t<Fn, CancellationToken>> {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  auto promise = std::make_shared<std::promise<ReturnType>>();
  auto future = promise->get_future();
  auto tokenFlag = std::make_shared<std::atomic_bool>(false);
  auto enqueueStopwatch = std::make_shared<diagnostics::Stopwatch>(diagnostics::Stopwatch::Start());
  auto promiseForWatcher = promise;

  if (deadlineMs > 0) {
    std::thread([promiseForWatcher, deadlineMs]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(deadlineMs));
      try {
        promiseForWatcher->set_exception(
            std::make_exception_ptr(std::runtime_error("job_deadline")));
      } catch (...) {}
    }).detach();
  }

  auto execute = [fn = std::move(fn), promise, tokenFlag, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
        try { promise->set_value(); } catch (...) {}
      } else {
        try { promise->set_value(fn(CancellationToken(tokenFlag))); } catch (...) {}
      }
    } catch (...) {
      try { promise->set_exception(std::current_exception()); } catch (...) {}
    }

    const auto runMs = runStopwatch.ElapsedMs();
    std::ostringstream log;
    log << "kind=" << static_cast<int>(kind)
        << " queue_wait_ms=" << queueWaitMs
        << " run_ms=" << runMs
        << " canceled=" << (canceled ? 'Y' : 'N');
    ECHO_LOG("RequestScheduler", log.str());
  };

  if (!EnqueueJob({std::move(execute), enqueueStopwatch})) {
    // Queue full or shutting down — fulfill the promise immediately
    // with an overload error so the future doesn't hang forever.
    try {
      promise->set_exception(
          std::make_exception_ptr(std::runtime_error("queue_full")));
    } catch (...) {}
  }
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

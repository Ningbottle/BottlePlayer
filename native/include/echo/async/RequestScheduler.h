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
#include "echo/async/RequestWatchdog.h"
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
  // Restart workers after a clean shutdown. Returns false if a previous
  // bounded shutdown abandoned workers, because those old threads may still
  // reference this scheduler object.
  bool Restart();

 private:
  struct Job {
    std::function<void()> execute;
    std::shared_ptr<diagnostics::Stopwatch> enqueueStopwatch;
  };

  void WorkerLoop();
  void StartWorkers();
  std::shared_ptr<std::atomic_bool> PrepareLatestToken(RequestKind kind, std::uint64_t& outGen);
  bool EnqueueJob(Job job);

  std::size_t workerCount_;
  std::vector<std::thread> workers_;
  std::deque<Job> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool shutdown_ = false;
  bool abandonedWorkers_ = false;
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

  // Separate claimed flag for the process watchdog (lazy-drop protocol).
  // On deadline: flip tokenFlag for cooperative cancel + set_exception.
  auto watchdogClaimed = std::make_shared<std::atomic_bool>(false);
  bool watchdogArmed = false;
  if (deadlineMs > 0) {
    // Admission control: a rejected Arm means the watchdog is saturated.
    // Fail the request with an explicit overload error instead of running
    // it without deadline enforcement (the watchdog exists because WinHTTP
    // per-op timeouts cannot be trusted alone).
    watchdogArmed = RequestWatchdog::Instance().Arm(
        deadlineMs, watchdogClaimed,
        [tokenFlag, promiseForWatcher]() {
          tokenFlag->store(true, std::memory_order_release);
          try {
            promiseForWatcher->set_exception(
                std::make_exception_ptr(std::runtime_error("job_deadline")));
          } catch (...) {}
        });
    if (!watchdogArmed) {
      try {
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("watchdog_overload")));
      } catch (...) {}
      return future;  // job is never enqueued
    }
  }

  auto execute = [fn = std::move(fn), promise, tokenFlag, kind, watchdogArmed, watchdogClaimed,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch->ElapsedMs();
    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    auto runStopwatch = diagnostics::Stopwatch::Start();

    // Stage 2 rule R ("claim decides"): whoever wins the CAS on
    // watchdogClaimed owns the promise; the loser must not touch it. The
    // worker claims AFTER fn completes but BEFORE fulfilling, so a business
    // completion that races a fired deadline deterministically yields to
    // job_deadline (the W3 window pinned by the resilience test).
    const auto claimForWorker = [watchdogClaimed]() -> bool {
      if (!watchdogClaimed) return true;  // no deadline armed
      bool expected = false;
      return watchdogClaimed->compare_exchange_strong(
          expected, true, std::memory_order_acq_rel);
    };

    try {
      if constexpr (std::is_void_v<ReturnType>) {
        fn(CancellationToken(tokenFlag));
        if (claimForWorker()) {
          // Release the deadline budget now — the armed entry is dead.
          if (watchdogArmed) {
            RequestWatchdog::Instance().Cancel(watchdogClaimed);
          }
          try { promise->set_value(); } catch (...) {}
        }
      } else {
        // Stage 1 (G1): fn's evaluation must NOT sit inside the same try as
        // set_value — a service exception swallowed by the inner catch left
        // the promise unsatisfied and callers saw "broken promise". fn's
        // exception falls through to the outer catch -> set_exception; only
        // set_value itself is ignored here. forward (not move): ReturnType
        // may be a reference (promise<T&>::set_value takes T&), which move
        // would reject at compile time.
        ReturnType value = fn(CancellationToken(tokenFlag));
        if (claimForWorker()) {
          if (watchdogArmed) {
            RequestWatchdog::Instance().Cancel(watchdogClaimed);
          }
          try { promise->set_value(std::forward<ReturnType>(value)); } catch (...) {}
        }
      }
    } catch (...) {
      if (claimForWorker()) {
        if (watchdogArmed) {
          RequestWatchdog::Instance().Cancel(watchdogClaimed);
        }
        try { promise->set_exception(std::current_exception()); } catch (...) {}
      }
      // Lost the claim: the deadline action fulfills the promise instead;
      // the business exception is dropped by rule R (and logged below).
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
    // Queue full or shutting down — claim under the same rule R, then
    // fulfill the promise with an overload error so the future doesn't
    // hang forever. If a fired deadline already claimed, its job_deadline
    // result wins and this rejection is dropped.
    bool expected = false;
    const bool won = !watchdogClaimed ||
        watchdogClaimed->compare_exchange_strong(
            expected, true, std::memory_order_acq_rel);
    if (won) {
      if (watchdogArmed) {
        RequestWatchdog::Instance().Cancel(watchdogClaimed);
      }
      try {
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("queue_full")));
      } catch (...) {}
    }
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

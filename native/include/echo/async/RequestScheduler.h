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

#include "echo/async/CancellationToken.h"
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
  // One-shot login-channel open (/login/qr/key). Appended, not inserted: the
  // numeric value reaches diagnostics logs, so existing kinds keep their
  // ordinals.
  LoginKey,
  Count,
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
  // Bounded shutdown cancels every active token and rejects queued jobs,
  // releasing their captures immediately. Workers retain only shared State
  // after a timeout, so the scheduler object can be destroyed safely. Their
  // user callables still own the lifetime of any external resources they use.
  // Repeated shutdown returns the original abandonment count.
  std::size_t Shutdown(std::chrono::milliseconds maxWait);
  // Restart workers after a clean shutdown. Returns false if a previous
  // bounded shutdown abandoned workers; that generation may still use the
  // caller's external resources.
  bool Restart();

 private:
  struct Job {
    RequestKind kind = RequestKind::Generic;
    std::function<void()> execute;
    std::shared_ptr<std::atomic_bool> tokenFlag;
    std::function<void()> reject;
  };

  struct State {
    explicit State(std::size_t workers)
        : activeTokens(workers), workerDone(workers, false),
          maxQueueSize(workers * 4), maxImageConcurrent(workers > 1 ? workers / 2 : 1) {}
    std::deque<Job> queue;
    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable stopped;
    bool shutdown = false;
    std::size_t abandonedWorkers = 0;
    std::size_t activeImageCount = 0;
    std::vector<std::shared_ptr<std::atomic_bool>> activeTokens;
    std::vector<bool> workerDone;
    const std::size_t maxQueueSize;
    const std::size_t maxImageConcurrent;
  };

  static void WorkerLoop(std::shared_ptr<State> state, std::size_t index);
  void StartWorkers();
  std::shared_ptr<std::atomic_bool> PrepareLatestToken(RequestKind kind, std::uint64_t& outGen);
  bool EnqueueJob(Job job, const std::shared_ptr<std::atomic_bool>& deadlineClaimed = {});
  static void RetireExpiredJob(const std::weak_ptr<State>& state,
                               const std::shared_ptr<std::atomic_bool>& tokenFlag);

  template <class T>
  static std::function<void()> ShutdownRejection(
      std::shared_ptr<std::promise<T>> promise,
      std::shared_ptr<std::atomic_bool> claimed = {}, bool armed = false) {
    return [promise = std::move(promise), claimed = std::move(claimed), armed] {
      if (claimed) {
        bool expected = false;
        if (!claimed->compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
        if (armed) RequestWatchdog::Instance().Cancel(claimed);
      }
      try {
        promise->set_exception(std::make_exception_ptr(std::runtime_error("scheduler_shutdown")));
      } catch (...) {}
    };
  }

  std::size_t workerCount_;
  std::vector<std::thread> workers_;
  std::shared_ptr<State> state_;
  std::mutex lifecycleMutex_;

  static constexpr std::size_t kKindCount = static_cast<std::size_t>(RequestKind::Count);
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
  auto enqueueStopwatch = diagnostics::Stopwatch::Start();

  auto execute = [fn = std::move(fn), promise, tokenFlag, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch.ElapsedMs();
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

  if (!EnqueueJob({kind, std::move(execute), tokenFlag, ShutdownRejection(promise)})) {
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
  auto enqueueStopwatch = diagnostics::Stopwatch::Start();
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
        [state = std::weak_ptr<State>(state_), tokenFlag, promiseForWatcher]() {
          tokenFlag->store(true, std::memory_order_release);
          try {
            promiseForWatcher->set_exception(
                std::make_exception_ptr(std::runtime_error("job_deadline")));
          } catch (...) {}
          RetireExpiredJob(state, tokenFlag);
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
    const auto queueWaitMs = enqueueStopwatch.ElapsedMs();

    auto runStopwatch = diagnostics::Stopwatch::Start();

    // B01 expired-while-queued gate. The watchdog CAS-claims the entry
    // BEFORE its cancel action runs, so claim=true (not tokenFlag) is the
    // authoritative "deadline fired" signal — there is a window where the
    // claim is taken but the action has not yet set the flag or the promise.
    // A successful false->false RMW is the logical queued->running point.
    // Unlike a load that reads the initial false, it participates in the
    // same modification order as the watchdog's claim; a later successful
    // watchdog CAS acquires this release. A failed strong CAS observes true
    // and skips fn even if the deadline action has not run yet.
    // Keeping false does NOT claim the result: a deadline after this start
    // point still cancels the running job and competes under rule R below.
    // Such a deadline may occur before the actual fn call; running work and
    // its side effects remain cooperatively cancellable, not retractable.
    bool unclaimed = false;
    if (watchdogArmed && !watchdogClaimed->compare_exchange_strong(
                             unclaimed, false, std::memory_order_acq_rel,
                             std::memory_order_acquire)) {
      std::ostringstream skipped;
      skipped << "kind=" << static_cast<int>(kind)
              << " queue_wait_ms=" << queueWaitMs
              << " skipped=job_deadline_before_start";
      ECHO_LOG("RequestScheduler", skipped.str());
      return;
    }

    const bool canceled = tokenFlag->load(std::memory_order_acquire);

    // Stage 2 rule R ("claim decides"): whoever changes watchdogClaimed
    // from false to true owns the promise; the loser must not touch it. The
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
        // exception falls through to the outer catch -> set_exception.
        // A delivery exception after we win the claim is delivered by that
        // same owner below; it must not compete for the claim a second time.
        // forward (not move): ReturnType
        // may be a reference (promise<T&>::set_value takes T&), which move
        // would reject at compile time.
        ReturnType value = fn(CancellationToken(tokenFlag));
        if (claimForWorker()) {
          if (watchdogArmed) {
            RequestWatchdog::Instance().Cancel(watchdogClaimed);
          }
          try {
            promise->set_value(std::forward<ReturnType>(value));
          } catch (...) {
            // A user-defined value may throw while entering the shared state.
            // We already own completion and have cancelled the watchdog.
            // Preserve that exception instead of abandoning the promise.
            try { promise->set_exception(std::current_exception()); } catch (...) {}
          }
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

  if (!EnqueueJob({kind, std::move(execute), tokenFlag,
                  ShutdownRejection(promise, watchdogClaimed, watchdogArmed)}, watchdogClaimed)) {
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
  auto enqueueStopwatch = diagnostics::Stopwatch::Start();

  auto execute = [fn = std::move(fn), promise, tokenFlag, myGen, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch.ElapsedMs();
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

  if (!EnqueueJob({kind, std::move(execute), tokenFlag, ShutdownRejection(promise)})) {
    promise->set_exception(std::make_exception_ptr(std::runtime_error("queue_full")));
  }
  return future;
}

template <class Fn>
void RequestScheduler::SubmitDetached(RequestKind kind, Fn fn) {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  auto tokenFlag = std::make_shared<std::atomic_bool>(false);
  auto enqueueStopwatch = diagnostics::Stopwatch::Start();

  auto execute = [fn = std::move(fn), tokenFlag, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch.ElapsedMs();
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

  EnqueueJob({kind, std::move(execute), tokenFlag, {}});
}

template <class Fn>
void RequestScheduler::SubmitLatestDetached(RequestKind kind, Fn fn) {
  using ReturnType = std::invoke_result_t<Fn, CancellationToken>;
  std::uint64_t myGen = 0;
  auto tokenFlag = PrepareLatestToken(kind, myGen);
  auto enqueueStopwatch = diagnostics::Stopwatch::Start();

  auto execute = [fn = std::move(fn), tokenFlag, myGen, kind,
                  enqueueStopwatch = std::move(enqueueStopwatch)]() mutable {
    const auto queueWaitMs = enqueueStopwatch.ElapsedMs();
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

  EnqueueJob({kind, std::move(execute), tokenFlag, {}});
}

}  // namespace echo::async

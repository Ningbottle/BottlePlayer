// RequestScheduler resilience contract tests (S1)
// Tests the per-job deadline and bounded shutdown added in S1.

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "echo/async/RequestScheduler.h"

using echo::async::RequestScheduler;
using echo::async::RequestKind;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
  do { \
    if (cond) { \
      std::cout << "  [ok] " << (msg) << "\n"; \
      ++g_passed; \
    } else { \
      std::cerr << "  [FAIL] " << (msg) << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      ++g_failed; \
    } \
  } while (0)

int main() {
  std::cout << "[Test] Testing RequestScheduler job deadline...\n";
  {
    RequestScheduler s(1);
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int {
          std::this_thread::sleep_for(std::chrono::seconds(10));
          return 42;
        },
        /*deadlineMs=*/100);
    bool gotException = false;
    try {
      (void)fut.get();
    } catch (const std::runtime_error&) {
      gotException = true;
    }
    CHECK(gotException, "job that exceeds deadlineMs throws runtime_error");
    // Use bounded Shutdown so the test doesn't wait the full 10s for the
    // worker to finish its uninterruptible sleep.
    s.Shutdown(std::chrono::milliseconds(500));
  }

  std::cout << "[Test] Testing RequestScheduler normal job completes...\n";
  {
    RequestScheduler s(1);
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 42; },
        /*deadlineMs=*/5000);
    CHECK(fut.get() == 42, "normal job returns 42 within deadline");
    s.Shutdown();
  }

  // A: deadline watcher must flip tokenFlag so workers exit via IsCancelled()
  // instead of waiting for the full nested HttpClient timeout.
  std::cout << "[Test] Testing RequestScheduler deadline cancels token early...\n";
  {
    RequestScheduler s(1);
    std::atomic<bool> exitedEarly{false};
    auto start = std::chrono::steady_clock::now();
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&exitedEarly](echo::async::CancellationToken token) -> int {
          for (int i = 0; i < 500; ++i) {
            if (token.IsCancellationRequested()) {
              exitedEarly.store(true, std::memory_order_release);
              return -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }
          return 0;
        },
        /*deadlineMs=*/100);
    bool gotDeadline = false;
    try {
      (void)fut.get();
    } catch (const std::runtime_error&) {
      gotDeadline = true;
    }
    // Wait briefly for the worker to observe cancel if the future already
    // resolved via set_exception while the body was still looping.
    for (int i = 0; i < 100 && !exitedEarly.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(gotDeadline || exitedEarly.load(),
          "deadline path surfaces job_deadline and/or early cancel exit");
    CHECK(exitedEarly.load(),
          "worker exits early via IsCancellationRequested after deadline");
    CHECK(elapsed < 1000,
          "cancel loop exits in <1s (not full HttpClient timeout)");
    s.Shutdown(std::chrono::milliseconds(500));
  }

  std::cout << "[Test] Testing RequestScheduler restart after clean shutdown...\n";
  {
    RequestScheduler s(1);
    auto first = s.Submit(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 7; });
    CHECK(first.get() == 7, "job before shutdown returns 7");
    s.Shutdown();

    CHECK(s.Restart(), "cleanly shut down scheduler restarts");
    auto second = s.Submit(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 8; });
    CHECK(second.get() == 8, "job after restart returns 8");
    s.Shutdown();
  }

  std::cout << "[Test] Testing RequestScheduler bounded shutdown (no hung workers) then restart...\n";
  {
    // Mirrors the real EchoShutdown path: bounded Shutdown(maxWait) on a
    // scheduler whose jobs all finished. No worker is hung, so abandoned MUST
    // be 0 and Restart() MUST succeed — this is the "app shutdown -> re-init"
    // path that the unbounded-Shutdown restart test above does NOT exercise.
    RequestScheduler s(1);
    auto first = s.Submit(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 7; });
    CHECK(first.get() == 7, "job before bounded shutdown returns 7");

    const auto abandoned = s.Shutdown(std::chrono::milliseconds(1000));
    CHECK(abandoned == 0, "bounded Shutdown with no hung workers abandons 0");
    CHECK(s.Restart(), "bounded clean shutdown allows Restart()");

    auto second = s.Submit(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 8; });
    CHECK(second.get() == 8, "job after bounded-shutdown restart returns 8");
    s.Shutdown();
  }

  std::cout << "[Test] Testing RequestScheduler queue full returns error...\n";
  {
    RequestScheduler s(1);  // 1 worker, maxQueue = 4
    // Fill the worker + queue: 1 running + 4 queued = 5 jobs
    std::atomic<int> barrierCount{0};
    auto barrier = [&]() {
      barrierCount.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::seconds(2));
    };
    s.SubmitDetached(RequestKind::Generic, [&](echo::async::CancellationToken) {
      barrier();
    });
    while (barrierCount.load() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < 4; i++) {
      s.SubmitDetached(RequestKind::Generic, [&](echo::async::CancellationToken) {
        barrier();
      });
    }
    // Queue should be full — next Submit should get queue_full error on future
    auto fut = s.Submit(RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 99; });
    bool gotQueueFull = false;
    try {
      fut.get();
    } catch (const std::runtime_error& e) {
      gotQueueFull = std::string(e.what()) == "queue_full";
    }
    CHECK(gotQueueFull, "queue-full Submit future throws queue_full immediately");
    s.Shutdown();
  }

  std::cout << "[Test] Testing RequestScheduler Shutdown with deadline-protected worker...\n";
  {
    RequestScheduler s(1);
    // Submit a long job with a short deadline. The deadline watcher will
    // fire the promise, and the worker continues sleeping in the background.
    // Shutdown joins the worker — but the worker will eventually check
    // shutdown_ and exit because EnqueueJob no longer runs synchronously
    // on shutdown (it returns false), so the worker loop ends quickly.
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int {
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          return 1;
        },
        /*deadlineMs=*/100);
    // Wait for the deadline to fire (future throws)
    try { (void)fut.get(); } catch (...) {}
    // Now Shutdown should join the still-sleeping worker within ~400ms
    auto start = std::chrono::steady_clock::now();
    s.Shutdown();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(elapsed < 5, "Shutdown completes within 5s after deadline fires");
  }

  std::cout << "[Test] Testing bounded Shutdown(3s) abandons hung workers...\n";
  {
    // Contract: a worker stuck in a long uninterruptible job (no deadline,
    // no cancellation) must NOT block Shutdown beyond the configured
    // deadline. The process is exiting so abandoning the worker is safe.
    // We use a 10s sleep instead of 60s to keep the test fast — the
    // 3s deadline still proves that Shutdown doesn't wait for the job.
    // The return value MUST report abandoned=1 to prove the worker was
    // actually abandoned (not just joined slowly).
    RequestScheduler s(1);
    std::atomic<bool> jobStarted{false};
    s.SubmitDetached(RequestKind::Generic,
        [&jobStarted](echo::async::CancellationToken) {
          jobStarted.store(true);
          std::this_thread::sleep_for(std::chrono::seconds(10));
        });
    while (!jobStarted.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto start = std::chrono::steady_clock::now();
    auto abandoned = s.Shutdown(std::chrono::milliseconds(3000));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    CHECK(elapsed < 3500,
          "Bounded Shutdown(3s) returns within 3.5s despite a 10s hung job");
    CHECK(abandoned == 1,
          "Bounded Shutdown(3s) abandons exactly 1 stuck worker (proves abandon path fired)");
    // The detached worker may still reference this scheduler, so Restart() must
    // refuse — this is what EchoShutdown relies on to skip global teardown.
    CHECK(!s.Restart(),
          "Restart() returns false after a bounded shutdown abandoned a worker");
  }

  std::cout << "[Test] Testing SubmitWithDeadline preserves service exceptions (non-void, value + reference)...\n";
  {
    // Stage 1 (vip-stability-remediation-plan): the non-void branch wrapped
    // set_value(fn(...)) in ONE try with an empty catch — fn's exception was
    // swallowed, the promise was never satisfied, and callers observed
    // std::future_error "broken promise" instead of the real service error.
    // Contract: fn's exception type AND message reach the future intact.
    // Reference ReturnType (int&) is pinned too: an earlier fix draft used
    // std::move(value), which no longer compiles against
    // std::promise<int&>::set_value(int&) (C2664) — forward keeps the
    // reference semantics.
    RequestScheduler s(1);
    int refSink = 0;
    auto futRef = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&refSink](echo::async::CancellationToken) -> int& {
          throw std::runtime_error("marker_service_failure");
        },
        /*deadlineMs=*/5000);
    bool gotMarkerRef = false;
    std::string whatRef;
    try {
      (void)futRef.get();
    } catch (const std::runtime_error& e) {
      gotMarkerRef = std::string(e.what()).find("marker_service_failure") != std::string::npos;
      whatRef = e.what();
    } catch (...) {
      whatRef = "non-runtime_error exception";
    }
    CHECK(gotMarkerRef, "reference-return job exception is rethrown with its message (got: " + whatRef + ")");

    auto futValue = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int {
          throw std::runtime_error("marker_service_failure");
        },
        /*deadlineMs=*/5000);
    bool gotMarkerValue = false;
    std::string whatValue;
    try {
      (void)futValue.get();
    } catch (const std::runtime_error& e) {
      gotMarkerValue = std::string(e.what()).find("marker_service_failure") != std::string::npos;
      whatValue = e.what();
    } catch (...) {
      whatValue = "non-runtime_error exception";
    }
    CHECK(gotMarkerValue, "value-return job exception is rethrown with its message (got: " + whatValue + ")");

    (void)refSink;
    s.Shutdown();
  }

  std::cout << "[Test] Testing SubmitWithDeadline preserves exceptions (void)...\n";
  {
    // Void branch had the correct two-layer structure already; pin it so the
    // fix can't regress it into the single-try shape.
    RequestScheduler s(1);
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> void {
          throw std::runtime_error("marker_void_failure");
        },
        /*deadlineMs=*/5000);
    bool gotMarker = false;
    try {
      fut.get();
    } catch (const std::runtime_error& e) {
      gotMarker = std::string(e.what()).find("marker_void_failure") != std::string::npos;
    }
    CHECK(gotMarker, "void job exception is rethrown with its message");
    s.Shutdown();
  }

  std::cout << "[Test] Testing SubmitWithDeadline deadline error keeps job_deadline message...\n";
  {
    // Deadline wins the race (watchdog set_exception first); the error must
    // remain the job_deadline runtime_error the deadline contract pins.
    //
    // Lifecycle note (review fix): the job must NOT outlive this scope as an
    // uninterruptible sleeper — after bounded Shutdown the scheduler object
    // is destroyed while the worker may still be inside the lambda. Use a
    // releasable barrier instead: the job parks on an atomic wait (no sleep
    // that ignores shutdown), we confirm the deadline fired, then release
    // and require a FULL Shutdown() join before the scope ends. No thread
    // may still reference scheduler members after destruction.
    RequestScheduler s(1);
    std::atomic<bool> releaseJob{false};
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&releaseJob](echo::async::CancellationToken token) -> int {
          // Cooperative, releasable block: honors both the released flag and
          // the deadline-cancel flag (mirrors HttpClientCancellationScope),
          // so the worker always makes progress and never outlives the
          // scheduler object.
          while (!releaseJob.load(std::memory_order_acquire) &&
                 !token.IsCancellationRequested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
          }
          return 42;
        },
        /*deadlineMs=*/100);
    bool gotDeadline = false;
    try {
      (void)fut.get();
    } catch (const std::runtime_error& e) {
      gotDeadline = std::string(e.what()) == "job_deadline";
    }
    CHECK(gotDeadline, "deadline expiry throws runtime_error(\"job_deadline\")");

    // Release the parked job, then join everything cleanly. The full
    // (unbounded) Shutdown must return promptly here — if it hung, this
    // test would block forever instead of leaking a dangling-worker window.
    releaseJob.store(true, std::memory_order_release);
    s.Shutdown();
  }

  std::cout << "[Test] All RequestScheduler resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}

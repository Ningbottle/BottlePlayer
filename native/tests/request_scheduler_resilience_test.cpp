// RequestScheduler resilience contract tests (S1)
// Tests the per-job deadline and bounded shutdown added in S1.

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
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
  }

  std::cout << "[Test] All RequestScheduler resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}

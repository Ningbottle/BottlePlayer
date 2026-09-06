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
#include "echo/async/RequestWatchdog.h"

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

  std::cout << "[Test] Testing watchdog: blocked action does not delay other deadlines (Stage 2)...\n";
  {
    // Stage 2 (root cause R6): the watchdog must deliver a short deadline's
    // ACTION while another action is blocked. The review probe caught two
    // things the first draft missed, both pinned here:
    //   - the assertion checks the action's COMPLETION SIGNAL, not the
    //     claimed flag — a claim without delivery must fail this test;
    //   - the blocking action is a releasable barrier and the test waits
    //     for its EXIT before leaving the scope. All test state lives in a
    //     shared_ptr captured by value, so even a CHECK failure on the
    //     wait-for-exit path cannot leave a watchdog action referencing
    //     dead stack locals (review P2-4).
    // The 500ms threshold proves BLOCKING ISOLATION only — it is not a
    // claim of 30ms precision.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();

    struct IsoState {
      std::atomic<bool> open{false};
      std::atomic<bool> entered{false};
      std::atomic<bool> exited{false};
      std::atomic<bool> shortRan{false};
    };
    auto st = std::make_shared<IsoState>();
    auto shortClaimed = std::make_shared<std::atomic_bool>(false);

    // 1. Close the barrier first so the blocking action is guaranteed to be
    // stuck BEFORE the short deadline is armed.
    wd.Arm(10, std::make_shared<std::atomic_bool>(false), [st] {
      st->entered.store(true, std::memory_order_release);
      while (!st->open.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      st->exited.store(true, std::memory_order_release);
    });
    for (int i = 0; i < 2000 && !st->entered.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(st->entered.load(), "blocking action entered the barrier (barrier is closed)");

    // 2. Arm a short deadline NOW and require its action to EXECUTE.
    const auto armedAt = std::chrono::steady_clock::now();
    wd.Arm(30, shortClaimed, [st] {
      st->shortRan.store(true, std::memory_order_release);
    });
    bool ran = false;
    for (int i = 0; i < 250; ++i) {
      if (st->shortRan.load(std::memory_order_acquire)) { ran = true; break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto honoredMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - armedAt).count();
    CHECK(ran && honoredMs < 500,
          "30ms deadline action EXECUTED within 500ms while another action is blocked (isolation; not a precision claim)");

    // 3. Release the barrier and wait for the blocking action to exit.
    st->open.store(true, std::memory_order_release);
    bool exited = false;
    for (int i = 0; i < 1000 && !exited; ++i) {
      exited = st->exited.load(std::memory_order_acquire);
      if (!exited) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(exited, "blocking action exited after release before scope end");
  }

  std::cout << "[Test] Testing deadline-vs-completion CAS arbitration (Stage 2 rule R)...\n";
  {
    // Review requirement: pick ONE rule and assert deterministic outcomes.
    // Rule R ("claim decides"): whichever side wins the claim CAS on
    // watchdogClaimed owns the promise; the loser must not touch it. The
    // scheduler worker claims BEFORE fulfilling; a business completion that
    // arrives after the deadline claimed must NOT overwrite the future.
    RequestScheduler s(1);
    std::atomic<bool> holdJob{false};
    std::atomic<bool> jobStarted{false};

    // A: job finishes well before deadline -> job's own result.
    auto futA = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> int { return 7; },
        /*deadlineMs=*/5000);
    CHECK(futA.get() == 7, "race A: completion claims first, future returns job result");

    // B: deadline fires while the job is parked. The claim race is
    // deterministic here (the parked job cannot claim), so the outcome must
    // be job_deadline — NOT "either outcome".
    auto futB = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&holdJob, &jobStarted](echo::async::CancellationToken) -> int {
          jobStarted.store(true, std::memory_order_release);
          while (!holdJob.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
          }
          return 42;
        },
        /*deadlineMs=*/120);
    std::shared_future<int> sharedB = futB.share();
    for (int i = 0; i < 2000 && !jobStarted.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    bool gotDeadline = false;
    try {
      (void)sharedB.get();
    } catch (const std::runtime_error& e) {
      gotDeadline = std::string(e.what()) == "job_deadline";
    }
    CHECK(gotDeadline, "race B: deadline claims while job parked -> future is job_deadline");

    // W3 (the review's key window): deadline has ALREADY claimed and its
    // action already ran; only NOW does the business job complete. The
    // second get() on the shared future must still be job_deadline — a
    // business-value overwrite here is the pre-rule-R defect.
    holdJob.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    bool stillDeadline = false;
    try {
      (void)sharedB.get();
    } catch (const std::runtime_error& e) {
      stillDeadline = std::string(e.what()) == "job_deadline";
    }
    CHECK(stillDeadline,
          "W3: business completion after deadline claim does not overwrite the future");

    // Join cleanly (releasable barrier protocol; worker observes nothing
    // left to do after the job body returned).
    s.Shutdown();
  }

  std::cout << "[Test] Cancel miss/duplicate/no-deadline leaves other deadlines intact...\n";
  {
    // Review P1: the first Cancel draft drained the whole heap and skipped
    // the restore when the target was not found — a normal no-deadline job
    // (no Arm, but Cancel on completion) wiped every OTHER request's
    // deadline. Regression matrix, all against one live victim deadline:
    //   1. miss (foreign claimed flag), 2. duplicate cancel, 3. cancel of
    //   an entry the timer already popped and ran, 4. a no-deadline
    //   scheduler job whose completion calls Cancel with a never-armed flag.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();

    struct RanState { std::atomic<bool> ran{false}; };
    auto victim = std::make_shared<RanState>();
    auto victimFlag = std::make_shared<std::atomic_bool>(false);
    wd.Arm(500, victimFlag, [victim] { victim->ran.store(true, std::memory_order_release); });

    // 1. Miss: foreign flag.
    wd.Cancel(std::make_shared<std::atomic_bool>(false));

    // 2. Duplicate: cancel, then cancel again (second is a miss).
    auto dupFlag = std::make_shared<std::atomic_bool>(false);
    wd.Arm(10000, dupFlag, [] {});
    wd.Cancel(dupFlag);
    wd.Cancel(dupFlag);

    // 3. Already-delivered: the entry ran and left the heap; cancel races
    // nothing but must still be a harmless miss.
    auto doneFlag = std::make_shared<std::atomic_bool>(false);
    wd.Arm(10, doneFlag, [] {});
    bool doneRan = false;
    for (int i = 0; i < 2000 && !doneRan; ++i) {
      doneRan = doneFlag->load(std::memory_order_acquire);
      if (!doneRan) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(doneRan, "cancel setup: short entry delivered and run");
    wd.Cancel(doneFlag);

    // 4. No-deadline scheduler job: never armed, but the completion path
    // calls Cancel with its never-armed flag.
    {
      RequestScheduler s(1);
      auto fut = s.SubmitWithDeadline(
          RequestKind::Generic,
          [](echo::async::CancellationToken) -> int { return 7; },
          /*deadlineMs=*/0);
      CHECK(fut.get() == 7, "no-deadline job completes (Cancel called with never-armed flag)");
      s.Shutdown();
    }

    // The victim deadline must have survived every path above.
    bool victimRan = false;
    for (int i = 0; i < 2000 && !victimRan; ++i) {
      victimRan = victim->ran.load(std::memory_order_acquire);
      if (!victimRan) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(victimRan, "cancel miss/duplicate/popped/no-deadline paths leave other deadlines intact");
  }

  std::cout << "[Test] watchdog saturation: hard backlog cap, no timer execution, overload feedback...\n";
  {
    // Review P2-3/P1 upgrades over the first saturation draft:
    //   - confirm the 4 workers entered the barrier BEFORE judging anything;
    //   - track running concurrency: a timer-executing (broken) or
    //     inline-fallback implementation pushes maxActive to 5;
    //   - the backlog has a HARD cap (kMaxPendingEntries): Arming beyond it
    //     must be REJECTED (bool false), not queued forever;
    //   - rejection feedback is observable at the scheduler level as an
    //     immediate "watchdog_overload" error, and the rejected job never
    //     starts.
    // All state lives in a shared_ptr captured by value (review P2-4: a
    // CHECK failure must not leave actions referencing dead stack locals).
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();

    struct SatState {
      std::atomic<bool> drain{false};
      std::atomic<int> completed{0};
      std::atomic<int> active{0};
      std::atomic<int> maxActive{0};
      std::atomic<bool> shortRan{false};
    };
    auto st = std::make_shared<SatState>();
    auto track = [st] {
      const int now = st->active.fetch_add(1, std::memory_order_acq_rel) + 1;
      int prev = st->maxActive.load(std::memory_order_relaxed);
      while (now > prev &&
             !st->maxActive.compare_exchange_weak(prev, now, std::memory_order_acq_rel)) {
      }
      while (!st->drain.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      st->active.fetch_sub(1, std::memory_order_acq_rel);
      st->completed.fetch_add(1, std::memory_order_acq_rel);
    };
    constexpr int kWorkers = static_cast<int>(RequestWatchdog::kExecutorThreads);

    // Phase A: occupy all 4 workers, confirmed by the active count.
    for (int i = 0; i < kWorkers; ++i) {
      wd.Arm(1, std::make_shared<std::atomic_bool>(false), track);
    }
    for (int i = 0; i < 2000 && st->active.load() < kWorkers; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(st->active.load() == kWorkers,
          "saturation setup: all 4 workers are inside blocked actions");

    // Phase B: a new short deadline must queue (not run) while workers are
    // blocked; concurrency must stay at 4 (timer executes nothing).
    wd.Arm(30, std::make_shared<std::atomic_bool>(false), [st] {
      st->shortRan.store(true, std::memory_order_release);
      // Counted like every other accepted action so phase E's delivery
      // accounting (completed == kMaxPendingEntries) is exact.
      st->completed.fetch_add(1, std::memory_order_acq_rel);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(st->maxActive.load() == kWorkers,
          "saturation: running concurrency never exceeds the 4-worker cap (timer executes nothing)");
    CHECK(!st->shortRan.load(),
          "saturation: the short action is queued behind blocked workers, not executed");

    // Phase C: fill the backlog to the hard cap, then Arm must reject.
    int accepted = 0;
    bool rejected = false;
    for (int i = 0; i < 4000; ++i) {
      if (!wd.Arm(1, std::make_shared<std::atomic_bool>(false), track)) {
        rejected = true;
        break;
      }
      ++accepted;
    }
    CHECK(rejected, "saturation: Arming beyond the backlog cap is rejected (bool false)");
    CHECK(accepted + kWorkers + 1 == static_cast<int>(RequestWatchdog::kMaxPendingEntries),
          "saturation: total accepted backlog equals kMaxPendingEntries exactly");

    // Phase D: rejection feedback reaches the scheduler caller.
    {
      RequestScheduler s(1);
      std::atomic<bool> overloadJobStarted{false};
      auto fut = s.SubmitWithDeadline(
          RequestKind::Generic,
          [&overloadJobStarted](echo::async::CancellationToken) -> int {
            overloadJobStarted.store(true, std::memory_order_release);
            return 1;
          },
          /*deadlineMs=*/1000);
      bool gotOverload = false;
      try {
        (void)fut.get();
      } catch (const std::runtime_error& e) {
        gotOverload = std::string(e.what()) == "watchdog_overload";
      }
      CHECK(gotOverload, "overload feedback: future resolves to watchdog_overload immediately");
      CHECK(!overloadJobStarted.load(), "overload feedback: the rejected job never runs");
      s.Shutdown();
    }

    // Phase E: release; every accepted deadline is delivered exactly once.
    st->drain.store(true, std::memory_order_release);
    const int kTotal = static_cast<int>(RequestWatchdog::kMaxPendingEntries);
    bool allDone = false;
    for (int i = 0; i < 1000 && !allDone; ++i) {
      allDone = st->completed.load(std::memory_order_acquire) == kTotal &&
                wd.DebugPendingActions() == 0;
      if (!allDone) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(allDone && st->completed.load() == kTotal,
          "saturation: all accepted actions delivered exactly once, pending back to 0");
    CHECK(st->maxActive.load() <= kWorkers,
          "saturation: concurrency cap held across the whole drain");
  }

  std::cout << "[Test] watchdog exit protocol: prompt destruction, queue drain, no UAF...\n";
  {
    // Review P2-4: the exit protocol had no real coverage ("CTest timeout
    // is not an exit-protocol test"). A LOCAL instance (public ctor)
    // exercises the destructor directly: it must return promptly even with
    // blocked running actions, the detached workers must drain the queued
    // accepted actions afterwards, and all state is owned by the actions'
    // shared_ptr captures so nothing references the destroyed object.
    using echo::async::RequestWatchdog;
    struct ExitState {
      std::atomic<bool> release{false};
      std::atomic<int> completed{0};
      std::atomic<int> entered{0};
    };
    auto st = std::make_shared<ExitState>();
    auto action = [st] {
      st->entered.fetch_add(1, std::memory_order_acq_rel);
      while (!st->release.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      st->completed.fetch_add(1, std::memory_order_acq_rel);
    };

    auto wd = std::make_unique<RequestWatchdog>();
    constexpr int kBlocked = static_cast<int>(RequestWatchdog::kExecutorThreads);
    constexpr int kQueued = 10;
    for (int i = 0; i < kBlocked + kQueued; ++i) {
      CHECK(wd->Arm(1, std::make_shared<std::atomic_bool>(false), action),
            "exit setup: local instance accepts its actions");
    }
    // Deterministic gate: exactly the 4 workers are INSIDE blocked actions
    // (entered count) and the full backlog (4 running + 10 queued = 14) is
    // still pending — i.e. the remaining 10 are queued, not executed. The
    // first draft polled `pending > kQueued`, which is always true here and
    // therefore verified nothing (review P2-4).
    for (int i = 0; i < 2000 && st->entered.load() < kBlocked; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(st->entered.load() == kBlocked,
          "exit setup: 4 workers are inside blocked actions");
    // The 10 must be IN THE QUEUE (timer dispatched them) before
    // destruction — after destruction the timer is stopped and heap-bound
    // entries would never be delivered.
    bool allQueued = false;
    for (int i = 0; i < 2000 && !allQueued; ++i) {
      allQueued = wd->DebugQueuedActions() == kQueued;
      if (!allQueued) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(allQueued && wd->DebugPendingActions() == kBlocked + kQueued,
          "exit setup: 4 running + 10 queued (dispatched), all still pending");

    const auto destroyStart = std::chrono::steady_clock::now();
    wd.reset();
    const auto destroyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - destroyStart).count();
    CHECK(destroyMs < 2000,
          "exit: destructor returns promptly despite blocked running actions");

    // Post-destruction the detached workers drain the 10 queued actions
    // once released — State is kept alive by their shared_ptr captures and
    // nothing touches the destroyed watchdog object.
    st->release.store(true, std::memory_order_release);
    const int kTotal = kBlocked + kQueued;
    bool drained = false;
    for (int i = 0; i < 1000 && !drained; ++i) {
      drained = st->completed.load(std::memory_order_acquire) == kTotal;
      if (!drained) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(drained && st->completed.load() == kTotal,
          "exit: queued actions drained by workers after destruction (shared State)");
  }

  std::cout << "[Test] W3 window: claim without fulfillment blocks business overwrite...\n";
  {
    // Discriminating test for rule R ("claim decides"). The review pointed
    // out the previous W3 draft released the business job only AFTER the
    // deadline action had fulfilled the promise — a completed promise can't
    // be overwritten by ANY implementation, so the test locked nothing.
    // The real window is: timer HAS claimed, deadline action is STILL
    // QUEUED (promise empty), and only then does the business job complete.
    // Mechanism: park all watchdog executor workers on barriers so the
    // deadline action cannot run.
    //   1. deadline fires while the business job is parked -> timer claims;
    //   2. business job is released and completes -> rule R: the worker
    //      must NOT fulfill the promise (wait_for(0) times out). A
    //      pre-Rule-R scheduler (set first, mark after) fills the business
    //      value here — this test was verified RED against that variant;
    //   3. barriers release -> the queued deadline action finally runs and
    //      fulfills the promise -> future resolves to job_deadline.
    using echo::async::RequestWatchdog;
    RequestWatchdog& wd = RequestWatchdog::Instance();

    struct BarrierState {
      std::atomic<bool> open{false};
      std::atomic<int> entered{0};
      std::atomic<int> exited{0};
    };
    // shared_ptr state: a CHECK failure must never leave a watchdog action
    // referencing dead stack locals (review P2-4).
    auto barriers = std::make_shared<BarrierState>();
    constexpr int kWorkers = static_cast<int>(RequestWatchdog::kExecutorThreads);
    for (int i = 0; i < kWorkers; ++i) {
      wd.Arm(5, std::make_shared<std::atomic_bool>(false), [barriers] {
        barriers->entered.fetch_add(1, std::memory_order_acq_rel);
        while (!barriers->open.load(std::memory_order_acquire)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        barriers->exited.fetch_add(1, std::memory_order_acq_rel);
      });
    }
    for (int i = 0; i < 2000 && barriers->entered.load() < kWorkers; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(barriers->entered.load() == kWorkers,
          "W3 setup: all watchdog workers parked on barriers (deadline action will queue)");

    RequestScheduler s(1);
    std::atomic<bool> holdJob{false};
    std::atomic<bool> jobFinished{false};
    const std::uint64_t claimedBaseline = wd.DebugClaimedCount();
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&holdJob, &jobFinished](echo::async::CancellationToken) -> int {
          while (!holdJob.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
          }
          jobFinished.store(true, std::memory_order_release);
          return 42;
        },
        /*deadlineMs=*/120);

    // 1. Deterministic claim gate: the timer has CAS-claimed the entry
    // (DebugClaimedCount increments exactly there). No sleep-guessing — the
    // action itself may still sit queued behind the barriers.
    bool timerClaimed = false;
    for (int i = 0; i < 2000 && !timerClaimed; ++i) {
      timerClaimed = wd.DebugClaimedCount() > claimedBaseline;
      if (!timerClaimed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(timerClaimed,
          "W3 setup: timer claimed the deadline entry (action queued behind barriers)");

    // 2. Business job completes AFTER the claim. While the barriers stay
    // closed the deadline action CANNOT run, so the only possible fulfiller
    // is the business path: poll readiness for a bounded budget. Rule R:
    // never ready. A pre-Rule-R scheduler fills the business value here and
    // is caught deterministically (verified RED against that variant).
    holdJob.store(true, std::memory_order_release);
    for (int i = 0; i < 2000 && !jobFinished.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    bool businessFulfilled = false;
    for (int i = 0; i < 250; ++i) {  // 500ms observation budget
      if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        businessFulfilled = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!businessFulfilled,
          "W3: business completion after claim (action still queued) does not fulfill the promise");

    // 3. Release the barriers: the queued deadline action fulfills.
    barriers->open.store(true, std::memory_order_release);
    bool exitedAll = false;
    for (int i = 0; i < 2000 && !exitedAll; ++i) {
      exitedAll = barriers->exited.load(std::memory_order_acquire) == kWorkers;
      if (!exitedAll) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(exitedAll, "W3 teardown: barrier actions exited");
    bool finalDeadline = false;
    try {
      (void)fut.get();
    } catch (const std::runtime_error& e) {
      finalDeadline = std::string(e.what()) == "job_deadline";
    }
    CHECK(finalDeadline, "W3: queued deadline action fulfills the promise with job_deadline");
    s.Shutdown();
  }

  std::cout << "[Test] All RequestScheduler resilience tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}

// R04 delivery-exception contract tests (agent B, 2026-09-28).
//
// R04 regression: the worker claims completion before set_value. Previously,
// a throwing copy/move while storing the result was swallowed after that
// claim, losing the original error; the promise was eventually abandoned
// and future.get() reported broken_promise on this toolchain.
//
// These tests require the original delivery exception, not broken_promise.
// The winning worker keeps completion ownership when set_value throws.
// They also pin, with throwing-value types, what happens on each
// branch, and pin the production value type's move traits at compile time:
// the only production SubmitWithDeadline instantiation (C_API.cpp) returns
// echo::core::CompatResponse, whose move-assign/ctor must stay noexcept
// (MSVC STL's set_value uses move-ASSIGNMENT for default-constructible
// types, placement-new otherwise).

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

#include "echo/async/RequestScheduler.h"
#include "echo/async/RequestWatchdog.h"
#include "echo/core/CompatApi.h"

using echo::async::RequestScheduler;
using echo::async::RequestKind;
using echo::core::CompatResponse;

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

// Production value-transfer traits. These do not make promise::set_value
// itself noexcept or exclude failures unrelated to the value's move.
static_assert(std::is_nothrow_move_assignable_v<CompatResponse>,
              "R04: CompatResponse move-assign must stay noexcept — MSVC "
              "promise<CompatResponse>::set_value uses move-assignment");
static_assert(std::is_nothrow_move_constructible_v<CompatResponse>,
              "R04: CompatResponse move-ctor must stay noexcept");
static_assert(std::is_default_constructible_v<CompatResponse>,
              "R04: MSVC set_value picks the move-assign path for "
              "default-constructible types — pin the chosen path");

// Default-constructible, throwing move-ASSIGN: reproduces the exact MSVC
// set_value(_Ty&&) code path (_Result = forward(_Val)) for the production
// shape. Move-ctor stays noexcept so fn's return value construction never
// throws — only the store into the shared state can.
struct ThrowingMoveAssign {
  int v = 0;
  explicit ThrowingMoveAssign(int x = 0) : v(x) {}
  ThrowingMoveAssign(const ThrowingMoveAssign&) = default;
  ThrowingMoveAssign(ThrowingMoveAssign&&) noexcept = default;
  ThrowingMoveAssign& operator=(const ThrowingMoveAssign&) = default;
  ThrowingMoveAssign& operator=(ThrowingMoveAssign&&) noexcept(false) {
    throw std::runtime_error("marker_set_value_throw");
  }
};

// Non-default-constructible, throwing move-CTOR: fn's `return T(42)` is
// guaranteed-elided (C++17 prvalue) so fn succeeds; the placement-new inside
// set_value is the only throw site. Proves the generic template hole for the
// other _Emplace_result branch.
struct ThrowMoveCtorNoDefault {
  int v;
  explicit ThrowMoveCtorNoDefault(int x) : v(x) {}
  ThrowMoveCtorNoDefault(const ThrowMoveCtorNoDefault&) = delete;
  ThrowMoveCtorNoDefault(ThrowMoveCtorNoDefault&&) noexcept(false) {
    throw std::runtime_error("marker_placement_new_throw");
  }
};

// A throwing copy on the referent must not affect reference delivery:
// promise<T&>::set_value(T&) stores its address rather than copying it.
struct ThrowingCopyAssign {
  int v = 0;
  ThrowingCopyAssign& operator=(const ThrowingCopyAssign&) {
    throw std::runtime_error("marker_ref_copy_throw");
  }
  ThrowingCopyAssign& operator=(ThrowingCopyAssign&&) noexcept {
    return *this;
  }
};

static bool IsExpectedException(const std::exception_ptr& ep,
                                const std::string& expected, std::string& what) {
  try {
    std::rethrow_exception(ep);
  } catch (const std::future_error& e) {
    what = std::string("future_error:") + e.code().message();
    return false;
  } catch (const std::exception& e) {
    what = e.what();
    return what == expected;
  } catch (...) {
    what = "non-std exception";
    return false;
  }
}

int main() {
  std::cout << std::unitbuf;

  std::cout << "[Test] R04: set_value throw (move-assign path) preserves its exception...\n";
  {
    // The worker WINS the claim (fn completed before the generous deadline),
    // then set_value throws. The SAME completion owner must publish the
    // original exception, without retrying the claim or abandoning the state.
    // CompatResponse's move-assignment is noexcept (asserted above), so
    // this value-transfer failure is reproduced with a generic seam.
    RequestScheduler s(1);
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> ThrowingMoveAssign {
          return ThrowingMoveAssign(42);  // guaranteed elision; no throw here
        },
        /*deadlineMs=*/5000);
    std::string what;
    bool gotMarker = false;
    try {
      auto v = fut.get();
      what = "returned value v=" + std::to_string(v.v) + " (BAD: swallowed throw produced a business value)";
    } catch (...) {
      gotMarker = IsExpectedException(std::current_exception(), "marker_set_value_throw", what);
    }
    std::cout << "  [info] future outcome: " << what << "\n";
    CHECK(gotMarker, "set_value delivery preserves the original move-assignment exception");
    s.Shutdown();
  }

  std::cout << "[Test] R04: set_value throw (placement-new path) preserves its exception...\n";
  {
    RequestScheduler s(1);
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> ThrowMoveCtorNoDefault {
          return ThrowMoveCtorNoDefault(7);  // C++17 guaranteed elision
        },
        /*deadlineMs=*/5000);
    std::string what;
    bool gotMarker = false;
    try {
      auto v = fut.get();
      what = "returned value v=" + std::to_string(v.v) + " (BAD)";
    } catch (...) {
      gotMarker = IsExpectedException(std::current_exception(), "marker_placement_new_throw", what);
    }
    std::cout << "  [info] future outcome: " << what << "\n";
    CHECK(gotMarker, "placement-new delivery preserves the original move-construction exception");
    s.Shutdown();
  }

  std::cout << "[Test] R04: reference branch — set_value stores the address, no copy, no throw...\n";
  {
    // MSVC promise<T&>::set_value(_Ty& _Val) stores _STD addressof(_Val)
    // (a bare pointer into the shared state) — the referent is never copied,
    // so a throwing-copy type cannot fail during value transfer here. The
    // audit's R04 exposure is confined to the VALUE branch; pinned with a
    // throwing-copy type to prove delivery still succeeds.
    RequestScheduler s(1);
    ThrowingCopyAssign sink;
    sink.v = 3;
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&sink](echo::async::CancellationToken) -> ThrowingCopyAssign& { return sink; },
        /*deadlineMs=*/5000);
    ThrowingCopyAssign* delivered = nullptr;
    try {
      auto& v = fut.get();
      delivered = &v;
    } catch (...) {
    }
    CHECK(delivered == &sink,
          "reference branch delivers the referent by identity; set_value throw is unreachable here");
    s.Shutdown();
  }

  std::cout << "[Test] R04: deadline claims first — throwing set_value never double-fulfills...\n";
  {
    // Rule R ownership must hold even for a throwing set_value: the deadline
    // claims while fn is parked; the worker later loses claimForWorker and
    // must not touch the promise. Future = job_deadline (NOT broken_promise,
    // NOT a business value, NOT a second fulfillment).
    RequestScheduler s(1);
    std::atomic<bool> holdJob{false};
    auto fut = s.SubmitWithDeadline(
        RequestKind::Generic,
        [&holdJob](echo::async::CancellationToken) -> ThrowingMoveAssign {
          while (!holdJob.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
          }
          return ThrowingMoveAssign(1);
        },
        /*deadlineMs=*/150);
    bool gotDeadline = false;
    std::string what;
    try {
      auto v = fut.get();
      what = "business value (BAD)";
      (void)v.v;
    } catch (const std::runtime_error& e) {
      gotDeadline = std::string(e.what()) == "job_deadline";
      what = e.what();
    } catch (...) {
      what = "other exception";
    }
    CHECK(gotDeadline, "deadline-claimed throwing job still delivers job_deadline (got: " + what + ")");
    holdJob.store(true, std::memory_order_release);
    s.Shutdown();
  }

  std::cout << "[Test] R04: non-deadline Submit delivers the set_value throw as an exception...\n";
  {
    // Submit keeps set_value inside the try whose catch calls set_exception,
    // so the same throw reaches the caller as a real exception. The deadline
    // variant now preserves the same exception when the worker owns completion.
    RequestScheduler s(1);
    auto fut = s.Submit(
        RequestKind::Generic,
        [](echo::async::CancellationToken) -> ThrowingMoveAssign {
          return ThrowingMoveAssign(5);
        });
    std::string what;
    bool gotMarker = false;
    try {
      auto v = fut.get();
      what = "business value (BAD)";
      (void)v.v;
    } catch (const std::exception& e) {
      what = e.what();
      gotMarker = std::string(what).find("marker_set_value_throw") != std::string::npos;
    } catch (...) {
      what = "non-std exception";
    }
    CHECK(gotMarker, "Submit's set_value throw is delivered (got: " + what + ")");
    s.Shutdown();
  }

  std::cout << "[Test] All R04 delivery-exception tests completed.\n";
  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}

# Storage Actor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace TLS + `write_mutex_` SQLite access with a single Storage Actor thread that serializes all DB work, with a lock-held lifecycle state machine that makes Submit/Close race-free.

**Architecture:** One dedicated actor thread owns `db_`. Public methods `Submit` tasks (promise/future, sync API preserved). Lifecycle enum `Closed | Starting | Open | Closing | Failed` is read/written only under `queue_mutex_`. Close transitions to `Closing` under the same lock so no task can enqueue after the close barrier. Tasks own callable + `shared_ptr<promise>` by value.

**Tech Stack:** C++20, SQLite3, CMake/CTest (`bottlemusic-check` preset), MSVC (vcvars64), nlohmann/json.

## Global Constraints

- Public `Database` API signatures **unchanged** (callers zero change).
- No connection pool; single connection on actor.
- No SQL/schema changes.
- No playback state machine (P1 #4) on this branch.
- TDD: **no production code without a failing test first**. r1 exploration was unsafe; worktree starts from **HEAD baseline** (already restored). Do not re-apply r1 patches.
- Work only on branch `codex/storage-actor` in `.worktrees/storage-actor`.
- `docs/` is gitignored; use `git add -f` for plan/spec when committing.
- Do not commit `native/build_tmp.bat`, hardcoded VS paths, or main-only junk.

**Spec:** `docs/superpowers/specs/2026-07-18-storage-actor-design.md` (r2).

**Build / test (Windows):**

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd native
cmake -S . --preset bottlemusic-check
cmake --build out/bottlemusic-check
ctest --preset bottlemusic-check --output-on-failure
```

If VS path differs, use the local install's `vcvars64.bat`. Do not commit path-specific bat files.

---

## File map

| Path | Role |
|------|------|
| `native/include/echo/storage/Database.h` | Public API + actor state, Submit template, remove TLS/write_mutex_ |
| `native/storage/Database.cpp` | Actor loop, Open/Close, Locked* methods, fallback via actor |
| `native/tests/database_actor_lifecycle_test.cpp` | **New** — Close/Submit races, open/close loops, multi-thread R/W |
| `native/tests/database_wal_concurrency_test.cpp` | Expand multi-thread stress; re-register CTest |
| `native/CMakeLists.txt` | Register new + WAL tests; link `EchoStorage`/`EchoCore` |
| `docs/superpowers/specs/2026-07-18-storage-actor-design.md` | r2 already written in worktree |
| `docs/superpowers/plans/2026-07-18-storage-actor.md` | This plan |

---

### Task 1: RED — actor lifecycle + concurrency tests

**Files:**
- Create: `native/tests/database_actor_lifecycle_test.cpp`
- Modify: `native/CMakeLists.txt` (register executable + test; temporary: may fail link/run until GREEN)
- Test: `ctest -R EchoDatabaseActorLifecycleTest`

**Interfaces:**
- Consumes: public `echo::storage::Database` only (`Open`, `Close`, `Initialize`, `SetJson`, `GetJson`)
- Produces: failing CTest proving missing/safe actor semantics

- [ ] **Step 1: Write the full lifecycle test file**

Create `native/tests/database_actor_lifecycle_test.cpp`:

```cpp
// Storage Actor lifecycle + concurrency contract tests (P1 #1).
// Requires actor serialization and lock-held Close/Submit protocol (design r2).

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "echo/storage/Database.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

std::filesystem::path MakeDbDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  return dir;
}

void ExpectNotAccepting(echo::storage::Database& db) {
  bool threw = false;
  try {
    db.SetJson("after-close", nlohmann::json{{"x", 1}});
  } catch (const std::runtime_error& e) {
    threw = true;
    const std::string msg = e.what();
    assert(msg.find("database_not_accepting") != std::string::npos);
  }
  assert(threw);
}

}  // namespace

int main() {
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  // ── 1) Close is idempotent ──
  {
    const auto dir = MakeDbDir("bm-actor-close-idempotent");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();
    db.SetJson("k", nlohmann::json{{"v", 1}});
    db.Close();
    db.Close();
    db.Close();
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] close_idempotent ok\n";
  }

  // ── 2) After Close, public writes fail without crash ──
  {
    const auto dir = MakeDbDir("bm-actor-after-close");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();
    db.Close();
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] after_close_reject ok\n";
  }

  // ── 3) Open/Close cycle × 100 ──
  {
    const auto dir = MakeDbDir("bm-actor-open-close-100");
    const auto path = dir / "t.db";
    for (int i = 0; i < 100; ++i) {
      echo::storage::Database db;
      db.Open(path);
      db.Initialize();
      db.SetJson("i", nlohmann::json{{"n", i}});
      auto j = db.GetJson("i");
      assert(j.has_value());
      assert((*j)["n"] == i);
      db.Close();
    }
    std::cout << "[ActorLifecycle] open_close_100 ok\n";
  }

  // ── 4) Multi-thread 1000 R/W (serializable consistency) ──
  {
    const auto dir = MakeDbDir("bm-actor-mt-1000");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    constexpr int kThreads = 8;
    constexpr int kPerThread = 125;  // 8 * 125 = 1000
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    std::atomic<int> failures{0};

    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        for (int i = 0; i < kPerThread; ++i) {
          const std::string key = "k-" + std::to_string(t) + "-" + std::to_string(i);
          try {
            db.SetJson(key, nlohmann::json{{"t", t}, {"i", i}});
            auto j = db.GetJson(key);
            if (!j.has_value() || (*j)["t"] != t || (*j)["i"] != i) {
              failures.fetch_add(1);
            }
          } catch (...) {
            failures.fetch_add(1);
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    assert(failures.load() == 0);
    db.Close();
    std::cout << "[ActorLifecycle] mt_1000_rw ok\n";
  }

  // ── 5) Concurrent Submit + Close (TOCTOU / no use-after-close) ──
  {
    const auto dir = MakeDbDir("bm-actor-submit-close-race");
    echo::storage::Database db;
    db.Open(dir / "t.db");
    db.Initialize();

    std::atomic<bool> start{false};
    std::atomic<int> write_ok{0};
    std::atomic<int> write_rejected{0};
    std::atomic<int> write_other_error{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
      writers.emplace_back([&, t] {
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        for (int i = 0; i < 200; ++i) {
          try {
            db.SetJson("race-" + std::to_string(t) + "-" + std::to_string(i),
                       nlohmann::json{{"i", i}});
            write_ok.fetch_add(1, std::memory_order_relaxed);
          } catch (const std::runtime_error& e) {
            const std::string msg = e.what();
            if (msg.find("database_not_accepting") != std::string::npos) {
              write_rejected.fetch_add(1, std::memory_order_relaxed);
            } else {
              write_other_error.fetch_add(1, std::memory_order_relaxed);
            }
          } catch (...) {
            write_other_error.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    std::thread closer([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      db.Close();
    });

    start.store(true, std::memory_order_release);
    for (auto& th : writers) th.join();
    closer.join();

    assert(write_other_error.load() == 0);
    // At least some work happened or was cleanly rejected; never hang/crash.
    assert(write_ok.load() + write_rejected.load() == 4 * 200);
    ExpectNotAccepting(db);
    std::cout << "[ActorLifecycle] submit_close_race ok (ok=" << write_ok.load()
              << " rejected=" << write_rejected.load() << ")\n";
  }

  // ── 6) Re-open after Close works ──
  {
    const auto dir = MakeDbDir("bm-actor-reopen");
    const auto path = dir / "t.db";
    echo::storage::Database db;
    db.Open(path);
    db.Initialize();
    db.SetJson("persist", nlohmann::json{{"a", 1}});
    db.Close();
    db.Open(path);
    db.Initialize();
    auto j = db.GetJson("persist");
    assert(j.has_value());
    assert((*j)["a"] == 1);
    db.Close();
    std::cout << "[ActorLifecycle] reopen ok\n";
  }

  std::cout << "[ActorLifecycle] all ok\n";
  return 0;
}
```

- [ ] **Step 2: Register the test in CMakeLists.txt**

In `native/CMakeLists.txt`, replace the WAL “manual only” comment block with:

```cmake
  add_executable(EchoDatabaseActorLifecycleTest tests/database_actor_lifecycle_test.cpp)
  target_include_directories(EchoDatabaseActorLifecycleTest PRIVATE include)
  target_link_libraries(EchoDatabaseActorLifecycleTest PRIVATE EchoStorage)
  add_test(NAME EchoDatabaseActorLifecycleTest COMMAND EchoDatabaseActorLifecycleTest)

  add_executable(EchoDatabaseWalConcurrencyTest tests/database_wal_concurrency_test.cpp)
  target_include_directories(EchoDatabaseWalConcurrencyTest PRIVATE include)
  target_link_libraries(EchoDatabaseWalConcurrencyTest PRIVATE EchoStorage)
  add_test(NAME EchoDatabaseWalConcurrencyTest COMMAND EchoDatabaseWalConcurrencyTest)
```

Add both names to `set(ECHO_NATIVE_TESTS ...)` list so PATH env applies.

- [ ] **Step 3: Expand WAL test with multi-thread stress**

Replace/extend `native/tests/database_wal_concurrency_test.cpp` body after existing single-thread checks with (keep existing WAL pragma + SetJson asserts first):

```cpp
  // Multi-thread: many concurrent SetJson/GetJson; actor must not SIGSEGV.
  {
    constexpr int kThreads = 6;
    constexpr int kIters = 50;
    std::vector<std::thread> threads;
    std::atomic<int> bad{0};
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        for (int i = 0; i < kIters; ++i) {
          const auto key = "w-" + std::to_string(t) + "-" + std::to_string(i);
          try {
            db.SetJson(key, nlohmann::json{{"i", i}});
            auto j = db.GetJson(key);
            if (!j || (*j)["i"] != i) bad.fetch_add(1);
          } catch (...) {
            bad.fetch_add(1);
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    assert(bad.load() == 0);
  }
```

Add `#include <atomic>`, `<thread>`, `<vector>` at top.

- [ ] **Step 4: Configure, build, run — confirm RED**

```bat
call "%VS_VCVARS%"
cd native
cmake -S . --preset bottlemusic-check
cmake --build out/bottlemusic-check --target EchoDatabaseActorLifecycleTest EchoDatabaseWalConcurrencyTest
ctest --preset bottlemusic-check -R "EchoDatabaseActorLifecycleTest|EchoDatabaseWalConcurrencyTest" --output-on-failure
```

**Expected RED (baseline HEAD, no actor):**
- Multi-thread sections: crash (access violation / assert) **or** data races / failures — **or**
- `ExpectNotAccepting` fails because Close does not throw `database_not_accepting` on later SetJson
- `submit_close_race` may hang, crash, or not enforce reject semantics

Record the actual failure mode in the commit message of the test-only commit. If a sub-case unexpectedly passes on TLS baseline, keep it (documents required post-actor behavior) and rely on cases that fail.

- [ ] **Step 5: Commit tests + CMake only**

```bash
git add native/tests/database_actor_lifecycle_test.cpp native/tests/database_wal_concurrency_test.cpp native/CMakeLists.txt
git add -f docs/superpowers/specs/2026-07-18-storage-actor-design.md docs/superpowers/plans/2026-07-18-storage-actor.md
git commit -m "$(cat <<'EOF'
test(native): RED storage actor lifecycle and WAL multi-thread harness

Register actor lifecycle + WAL concurrency CTests before implementation.
EOF
)"
```

---

### Task 2: GREEN — actor state machine + Submit under queue_mutex_

**Files:**
- Modify: `native/include/echo/storage/Database.h`
- Modify: `native/storage/Database.cpp`
- Test: same CTests as Task 1

**Interfaces:**
- Consumes: Task 1 tests; design r2 Close/Submit protocol
- Produces: race-free actor; public API unchanged

- [ ] **Step 1: Rewrite Database.h private section for actor**

Keep public API identical. Private design (illustrative — implement fully):

```cpp
 private:
  enum class ActorState { Closed, Starting, Open, Closing, Failed };

  std::thread actor_;
  mutable std::queue<std::function<void()>> task_queue_;
  mutable std::mutex queue_mutex_;
  mutable std::condition_variable queue_cv_;
  ActorState state_{ActorState::Closed};
  std::thread::id actor_tid_{};

  void StartActorUnlocked();  // call only with queue_mutex_ held where required
  void StopActor();           // Close path: drain, join, state=Closed
  void ActorLoop();

  template <typename F>
  auto Submit(F&& fn) const -> std::invoke_result_t<F> {
    using R = std::invoke_result_t<F>;
    auto promise = std::make_shared<std::promise<R>>();
    auto future = promise->get_future();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (state_ != ActorState::Open) {
        throw std::runtime_error("database_not_accepting");
      }
      if (std::this_thread::get_id() == actor_tid_) {
        throw std::runtime_error("actor_reentrancy");
      }
      task_queue_.emplace([fn = std::forward<F>(fn), promise]() mutable {
        try {
          if constexpr (std::is_void_v<R>) {
            fn();
            promise->set_value();
          } else {
            promise->set_value(fn());
          }
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
    }
    queue_cv_.notify_one();
    return future.get();
  }

  // path_ only written/read on actor thread via OpenLocked
  std::filesystem::path path_;
  // ... Locked* methods, db_, no write_mutex_, no TLS ...
```

Include headers: `<atomic>` only if needed; prefer state under mutex. Need `<future>`, `<queue>`, `<thread>`, `<condition_variable>`, `<functional>`.

- [ ] **Step 2: Implement ActorLoop / Start / Close in Database.cpp**

**StartActor (exception-safe):**

```cpp
void Database::StartActor() {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (state_ == ActorState::Open || state_ == ActorState::Starting) return;
  state_ = ActorState::Starting;
  try {
    actor_ = std::thread([this] {
      {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        actor_tid_ = std::this_thread::get_id();
        state_ = ActorState::Open;
      }
      queue_cv_.notify_all();
      ActorLoop();
    });
  } catch (...) {
    state_ = ActorState::Failed;
    actor_tid_ = {};
    throw;
  }
  // Wait until actor sets Open (or use condition)
  // Simpler alternative: set Open only after thread starts via handshake promise
}
```

Prefer a `std::promise<void> started` set as first line of actor after `actor_tid_` assignment so `Open` does not race Submit before Open state.

**Close (same lock for Closing):**

```cpp
void Database::Close() {
  std::shared_ptr<std::promise<void>> done;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (state_ == ActorState::Closed || state_ == ActorState::Closing) {
      // if Closing, fall through to join wait outside
    }
    if (state_ == ActorState::Closed) return;
    if (state_ != ActorState::Open && state_ != ActorState::Starting &&
        state_ != ActorState::Failed) {
      // handle Failed: join if joinable, state=Closed
    }
    state_ = ActorState::Closing;
    done = std::make_shared<std::promise<void>>();
    auto fut_holder = done;
    task_queue_.emplace([this, fut_holder] {
      try {
        CloseLocked();
        fut_holder->set_value();
      } catch (...) {
        fut_holder->set_exception(std::current_exception());
      }
    });
  }
  queue_cv_.notify_one();
  if (done) {
    done->get_future().wait();  // or get() to surface errors
  }
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    // signal loop exit after drain: use stop flag or state Closing + empty
  }
  queue_cv_.notify_one();
  if (actor_.joinable()) actor_.join();
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    state_ = ActorState::Closed;
    actor_tid_ = {};
  }
}
```

**ActorLoop:** while true, wait for `!queue.empty() || state_==Closing` (and after close task + empty, exit). When `state_==Closing` and queue empty **after** CloseLocked ran, return.

Invariant: Submit requires `state_==Open` under lock → no enqueue after Closing.

- [ ] **Step 3: Route all public methods through Submit; Open passes path by value**

```cpp
void Database::Open(const std::filesystem::path& path) {
  StartActor();  // leaves state Open
  const auto p = path;  // copy
  Submit([this, p] { OpenLocked(p); });
}

void Database::Initialize() {
  Submit([this] { InitializeSchema(); });  // or InitializeLocked
}

void Database::SetJson(...) {
  Submit([&] { SetJsonLocked(...); });  // better: capture key/value by value
}
```

**Capture rule:** never capture stack refs of arguments that outlive only the caller wait — prefer **by-value** captures of `key`, `value`, `sql`, `params` into the lambda passed to Submit (Submit itself moves the callable into the queue).

Remove: `write_mutex_`, TLS `ReadDb` / `TlsReads`, dual-connection read path. `ExecuteQuery*` → `Submit` → `ExecuteQueryBoundLocked` on `db_`.

- [ ] **Step 4: Build lifecycle + WAL tests — GREEN**

```bat
cmake --build out/bottlemusic-check --target EchoDatabaseActorLifecycleTest EchoDatabaseWalConcurrencyTest
ctest --preset bottlemusic-check -R "EchoDatabaseActorLifecycleTest|EchoDatabaseWalConcurrencyTest" --output-on-failure
```

**Expected:** all PASS, no crash.

- [ ] **Step 5: Full native CTest**

```bat
ctest --preset bottlemusic-check --output-on-failure
```

**Expected:** all tests PASS (count ≥ 11 if two new tests added).

- [ ] **Step 6: Commit implementation**

```bash
git add native/include/echo/storage/Database.h native/storage/Database.cpp
git commit -m "$(cat <<'EOF'
feat(native): storage actor with lock-held lifecycle for Close/Submit

Serialize all SQLite access on one actor thread; Closing state blocks
new enqueue under queue_mutex_ to eliminate Close TOCTOU.
EOF
)"
```

---

### Task 3: Stress + verification gates

**Files:** none required if Task 2 green; optional comment cleanup in `Database.h` (remove obsolete TLS snapshot docs, document actor linearizability).

- [ ] **Step 1: Repeat actor lifecycle test 10×**

```bat
for /L %i in (1,1,10) do ctest --preset bottlemusic-check -R EchoDatabaseActorLifecycleTest --output-on-failure
```

All 10 must pass.

- [ ] **Step 2: Update public comments on Database.h**

Replace WAL TLS snapshot comments with: actor serializes all access; cross-thread SetJson then GetJson is linearizable (happens-before via queue).

- [ ] **Step 3: Optional Rust/UI smoke (if time)**

From `ui/`: `cargo test --manifest-path src-tauri/Cargo.toml --no-default-features` (CI style). Not required to block actor merge if pure native storage change with no FFI signature change — but run if C API touches storage startup.

- [ ] **Step 4: Final commit if comments changed**

```bash
git add native/include/echo/storage/Database.h
git commit -m "docs(native): document storage actor linearizability"
```

- [ ] **Step 5: Stop — do not start P1 #4 playback state machine**

Open PR from `codex/storage-actor` when gates green. Playback is a **separate** branch/commit after this merges.

---

## Self-review (plan vs spec r2)

| Spec requirement | Task |
|------------------|------|
| Single actor thread owns db_ | Task 2 |
| Submit under lock, state==Open | Task 2 |
| Close → Closing under same lock; no post-close enqueue | Task 2 + Task 1 race test |
| Tasks own callable + shared_ptr promise | Task 2 |
| path_ not written off actor | Task 2 Open by-value |
| Exception-safe Start | Task 2 StartActor try/catch |
| Reentrancy detection | Task 2 Submit actor_tid_ check |
| Remove TLS / write_mutex_ | Task 2 |
| Fallback via actor | Task 2 |
| Re-register WAL multi-thread | Task 1 |
| Lifecycle tests (race, reject, 100×, 1000 R/W) | Task 1 |
| Public API unchanged | Global + Task 2 |
| No P1 #4 | Task 3 stop |

**Placeholder scan:** none intentional. VS `vcvars` path is machine-local — use env discovery, do not commit bat.

**Type consistency:** `ActorState` enum, error string `database_not_accepting`, test binary `EchoDatabaseActorLifecycleTest` used consistently.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-18-storage-actor.md`.

**Worktree ready:** `C:\BottleMusic\.worktrees\storage-actor` on `codex/storage-actor`  
**main:** Database WIP removed (baseline); only unrelated untracked local files may remain in primary tree.

Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — this session, executing-plans with checkpoints  

Which approach?

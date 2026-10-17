// Two Database instances on one SQLite file stand in for two app processes
// sharing one app-data directory: each has its own storage actor, so only
// the SQLite-level transaction in UpdateJson can keep their read-modify-write
// operations from interleaving.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "echo/core/Dto.h"
#include "echo/storage/Database.h"
#include "echo/storage/SessionRepository.h"

#if !defined(ECHO_NATIVE_HAS_SQLITE)
#error "EchoDatabaseCrossConnectionTest requires SQLite"
#endif

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "[CrossConnection] CHECK FAILED: %s [%s:%d]\n", msg, \
                   __FILE__, __LINE__);                                         \
      std::fflush(stderr);                                                      \
      std::exit(1);                                                             \
    }                                                                           \
  } while (0)

namespace {

using echo::storage::Database;
using echo::storage::SessionRepository;
using Clock = std::chrono::steady_clock;

std::filesystem::path FreshDbPath(const char* name) {
  static std::atomic<unsigned> next{0};
  const auto dir = std::filesystem::temp_directory_path() /
                   (std::string("bm-cross-connection-") + name + "-" +
                    std::to_string(Clock::now().time_since_epoch().count()) + "-" +
                    std::to_string(next.fetch_add(1)));
  std::filesystem::create_directories(dir);
  return dir / "bottlemusic.db";
}

struct TwoConnections {
  Database first;
  Database second;
  explicit TwoConnections(const char* name) {
    const auto path = FreshDbPath(name);
    first.Open(path);
    first.Initialize();
    second.Open(path);
    second.Initialize();
  }
  ~TwoConnections() {
    first.Close();
    second.Close();
  }
};

echo::core::SessionInfo SyntheticSession() {
  echo::core::SessionInfo session;
  session.userId = "synthetic-account-only";
  session.token = "synthetic-token-only";
  session.nickname = "before";
  return session;
}

std::uint64_t StoredGeneration(Database& db) {
  const auto payload = db.GetJson("session.info");
  CHECK(payload && payload->is_object() && payload->contains("gen") &&
            (*payload)["gen"].is_number_unsigned(),
        "stored session payload carries an unsigned generation");
  return (*payload)["gen"].get<std::uint64_t>();
}

}  // namespace

int main() {
  // 1) Concurrent increments through two connections lose no update.
  {
    TwoConnections dbs("counter");
    dbs.first.SetJson("counter", nlohmann::json{{"n", 0}});
    constexpr int kThreadsPerConnection = 2;
    constexpr int kIncrements = 50;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreadsPerConnection * 2; ++t) {
      Database& db = t % 2 == 0 ? dbs.first : dbs.second;
      threads.emplace_back([&db, &failures] {
        for (int i = 0; i < kIncrements; ++i) {
          try {
            db.UpdateJson("counter", [](std::optional<nlohmann::json> current) {
              auto next = current.value_or(nlohmann::json{{"n", 0}});
              next["n"] = next["n"].get<int>() + 1;
              return std::optional<nlohmann::json>(next);
            });
          } catch (...) {
            failures.fetch_add(1);
          }
        }
      });
    }
    for (auto& thread : threads) thread.join();
    CHECK(failures.load() == 0, "no increment failed with SQLITE_BUSY");
    const auto counter = dbs.second.GetJson("counter");
    CHECK(counter && (*counter)["n"] == kThreadsPerConnection * 2 * kIncrements,
          "every increment from both connections is preserved");
    std::cout << "[CrossConnection] concurrent_increments_lose_nothing ok\n";
  }

  // 2) A logout on connection B while connection A is between its generation
  //    check and its write waits for A, then supersedes it. The pre-fix
  //    interleaving (B clears inside A's window, A writes the old session
  //    back with the old generation) must be impossible.
  {
    TwoConnections dbs("patch-vs-clear");
    SessionRepository repoA(dbs.first);
    SessionRepository repoB(dbs.second);
    const auto generation = repoA.Save(SyntheticSession());

    std::promise<void> patchEntered;
    auto patchEnteredFuture = patchEntered.get_future();
    std::atomic<bool> clearFinished{false};
    std::atomic<bool> clearFinishedDuringPatch{true};
    std::atomic<bool> patchApplied{false};
    std::string patchError;
    std::string clearError;

    std::thread patch([&] {
      try {
        patchApplied = repoA.PatchIfGeneration(generation, [&](auto& session) {
          patchEntered.set_value();
          std::this_thread::sleep_for(std::chrono::milliseconds(400));
          clearFinishedDuringPatch = clearFinished.load();
          session.nickname = "late profile response";
        });
      } catch (const std::exception& error) {
        patchError = error.what();
      }
    });
    patchEnteredFuture.wait();
    const auto clearStarted = Clock::now();
    std::thread logout([&] {
      try {
        repoB.Clear();
        clearFinished = true;
      } catch (const std::exception& error) {
        clearError = error.what();
      }
    });
    patch.join();
    logout.join();
    const auto clearWaited = Clock::now() - clearStarted;

    CHECK(patchError.empty(), "patch did not fail");
    CHECK(clearError.empty(), "logout waited for the lock instead of failing busy");
    CHECK(!clearFinishedDuringPatch.load(), "logout could not commit inside the patch window");
    CHECK(clearWaited >= std::chrono::milliseconds(200), "logout actually waited for the patch");
    CHECK(patchApplied.load(), "patch committed against the generation it checked");
    const auto final = repoB.LoadSnapshot();
    CHECK(!final.session.has_value(), "logout is final; the late patch did not revive it");
    CHECK(!repoA.LoadSnapshot().session.has_value(), "both connections observe the logout");
    CHECK(StoredGeneration(dbs.first) == generation + 1,
          "logout advanced past the generation the patch kept");
    std::cout << "[CrossConnection] patch_window_blocks_cross_connection_logout ok\n";
  }

  // 3) A patch from a request that captured the generation before a logout
  //    on the other connection is rejected and leaves the logout intact.
  {
    TwoConnections dbs("clear-then-patch");
    SessionRepository repoA(dbs.first);
    SessionRepository repoB(dbs.second);
    const auto generation = repoA.Save(SyntheticSession());
    repoB.Clear();
    const bool applied = repoA.PatchIfGeneration(
        generation, [](auto& session) { session.nickname = "late"; });
    CHECK(!applied, "stale generation patch rejected across connections");
    CHECK(!repoB.LoadSnapshot().session.has_value(), "logout stays in effect");
    CHECK(StoredGeneration(dbs.second) == generation + 1, "generation never moves backwards");
    std::cout << "[CrossConnection] stale_patch_after_cross_connection_logout_rejected ok\n";
  }

  // 4) A throwing mutator writes nothing and releases the write lock at once.
  {
    TwoConnections dbs("mutator-throws");
    dbs.first.SetJson("k", nlohmann::json{{"v", 1}});
    bool threw = false;
    try {
      dbs.first.UpdateJson("k", [](std::optional<nlohmann::json>) -> std::optional<nlohmann::json> {
        throw std::runtime_error("mutator-boom");
      });
    } catch (const std::runtime_error& error) {
      threw = std::string(error.what()) == "mutator-boom";
    }
    CHECK(threw, "the mutator's own exception propagates unchanged");
    const auto started = Clock::now();
    dbs.second.SetJson("other", nlohmann::json{{"v", 2}});
    CHECK(Clock::now() - started < std::chrono::seconds(2),
          "the other connection is not left waiting on an abandoned transaction");
    const auto kept = dbs.second.GetJson("k");
    CHECK(kept && (*kept)["v"] == 1, "rolled-back update left the value untouched");
    const auto applied = dbs.first.UpdateJson("k", [](std::optional<nlohmann::json> current) {
      auto next = *current;
      next["v"] = 3;
      return std::optional<nlohmann::json>(next);
    });
    CHECK(applied && (*dbs.second.GetJson("k"))["v"] == 3,
          "the connection commits normally after a rollback");
    std::cout << "[CrossConnection] throwing_mutator_rolls_back_and_unlocks ok\n";
  }

  // 5) Inside a caller-owned transaction UpdateJson nests as a savepoint: the
  //    outer transaction decides the outcome and survives a failed update.
  {
    TwoConnections dbs("nested");
    dbs.first.Execute("BEGIN IMMEDIATE;");
    dbs.first.UpdateJson("n", [](std::optional<nlohmann::json>) {
      return std::optional<nlohmann::json>(nlohmann::json{{"v", 1}});
    });
    bool threw = false;
    try {
      dbs.first.UpdateJson("n", [](std::optional<nlohmann::json>) -> std::optional<nlohmann::json> {
        throw std::runtime_error("nested-boom");
      });
    } catch (const std::runtime_error&) {
      threw = true;
    }
    CHECK(threw, "nested mutator failure propagates");
    const auto inside = dbs.first.GetJson("n");
    CHECK(inside && (*inside)["v"] == 1,
          "failed nested update only rolled back its own savepoint");
    dbs.first.Execute("ROLLBACK;");
    CHECK(!dbs.first.GetJson("n").has_value(), "outer rollback discards the nested update");

    dbs.first.Execute("BEGIN IMMEDIATE;");
    dbs.first.UpdateJson("n", [](std::optional<nlohmann::json>) {
      return std::optional<nlohmann::json>(nlohmann::json{{"v", 2}});
    });
    dbs.first.Execute("COMMIT;");
    const auto committed = dbs.second.GetJson("n");
    CHECK(committed && (*committed)["v"] == 2, "outer commit publishes the nested update");
    std::cout << "[CrossConnection] nested_update_uses_savepoint ok\n";
  }

  std::cout << "[CrossConnection] all ok\n";
  return 0;
}

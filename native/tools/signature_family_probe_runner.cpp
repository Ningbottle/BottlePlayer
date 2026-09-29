// EchoSignatureFamilyProbeRunner — live trigger for the debug-only
// /diagnostics/signature-family A/B probe.
//
// Opens the SAME production database the app uses (echo::storage::AppPaths:
// ECHO_NATIVE_DATA_DIR override or %LOCALAPPDATA%\EchoMusicNative), runs
// HandleSignatureFamilyProbe ONCE with the real transports, and prints the
// redacted JSON summary (fingerprints + statuses only; never tokens, account
// IDs or signed URLs).
//
// Rules honoured here:
//   * Debug builds only — the diag route does not exist in Release (#ifndef
//     NDEBUG in CompatApi.cpp), so a Release build of this tool refuses to run
//     instead of silently probing a packaged app.
//   * This is an instrument, not a fixture: the round is only usable for
//     family selection when data.usable_for_selection=true and the verdicts
//     are repeatable (reverse-order retest per the plan's judgement table).
//
// Usage:
//   EchoSignatureFamilyProbeRunner.exe [budget_ms] [db_path] [reverse]
//   db_path defaults to the app's production database (ECHO_NATIVE_DATA_DIR
//   or %LOCALAPPDATA%\EchoMusicNative); pass the app's bottlemusic.db
//   explicitly when the Tauri app data dir differs. `reverse` runs the
//   Concept family first (judgement-table retest order).
#include <iostream>

#ifndef NDEBUG

#include <filesystem>
#include <string>

#include "echo/core/CompatRoutes.h"
#include "echo/core/RequestDeadlines.h"
#include "echo/storage/AppPaths.h"
#include "echo/storage/Database.h"

int main(int argc, char** argv) {
  long long budgetMs = echo::core::kSignatureFamilyProbeBudgetMs;
  if (argc > 1) {
    try {
      budgetMs = std::stoll(argv[1]);
    } catch (...) {
      std::cerr << "invalid budget_ms argument: " << argv[1] << std::endl;
      return 2;
    }
  }
  std::filesystem::path dbPath = echo::storage::GetDefaultDatabasePath();
  if (argc > 2) {
    dbPath = std::filesystem::path(argv[2]);
  }
  const bool reverseOrder = argc > 3 && std::string(argv[3]) == "reverse";

  std::cout << "database: " << dbPath.string() << std::endl;
  if (!std::filesystem::exists(dbPath)) {
    std::cerr << "database does not exist (log in via the app first)" << std::endl;
    return 2;
  }

  echo::storage::Database db;
  db.Open(dbPath);        // throws on failure (actor infra)
  db.Initialize();

  const auto response = echo::core::HandleSignatureFamilyProbe(
      db, {}, {}, budgetMs, reverseOrder);
  db.Close();

  std::cout << response.body.dump(2) << std::endl;
  if (response.body.value("status", 0) != 1) {
    std::cerr << "probe did not run (see error_code)" << std::endl;
    return 2;
  }
  const auto& data = response.body["data"];
  if (!data.value("usable_for_selection", false)) {
    std::cerr << "round is NOT usable for family selection (interfered/NOT_RUN)"
              << std::endl;
    return 3;
  }
  return 0;
}

#else  // NDEBUG

int main() {
  std::cerr
      << "signature-family probe is a DEBUG-only diagnostic; the route does "
         "not exist in Release builds. Build with the Debug configuration."
      << std::endl;
  return 1;
}

#endif  // !NDEBUG

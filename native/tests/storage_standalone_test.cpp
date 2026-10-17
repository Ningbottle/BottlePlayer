#include "echo/storage/SessionRepository.h"
#include "echo/storage/DeviceRepository.h"
#include <filesystem>
#include <chrono>
#include <stdexcept>

int main() {
  const auto path = std::filesystem::temp_directory_path() /
      ("bm-storage-standalone-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
  echo::storage::Database db;
  db.Open(path); db.Initialize();
  echo::storage::SessionRepository sessions(db);
  echo::model::SessionInfo session;
  session.userId = "synthetic"; session.token = "standalone-token";
  sessions.Save(session);
  if (!sessions.Load() || sessions.Load()->token != session.token) throw std::runtime_error("standalone roundtrip");
  echo::storage::DeviceRepository(db).Save({.dfid="-", .guid="standalone"});
  const auto snapshot = sessions.CaptureIdentity();
  if (snapshot.device.guid != "standalone" || snapshot.version.deviceRevision != 1) throw std::runtime_error("standalone identity");
  sessions.ClearWithDevice(); db.Close();
  std::filesystem::remove(path);
}

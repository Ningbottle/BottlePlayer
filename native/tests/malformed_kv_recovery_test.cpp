// Valid JSON with the wrong shape in device.info / session.info must read as
// "no valid record" instead of throwing, and the explicit recovery routes
// (logout, /settings/device?clear=1) must succeed over such a record.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "echo/core/C_API.h"
#include "echo/core/DeviceService.h"
#include "echo/core/Dto.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "[MalformedKv] CHECK FAILED: %s [%s:%d]\n", msg,    \
                   __FILE__, __LINE__);                                        \
      std::fflush(stderr);                                                     \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

namespace {

using echo::storage::Database;
using echo::storage::DeviceRepository;
using echo::storage::SessionRepository;
using nlohmann::json;

std::filesystem::path FreshDir(const std::string& name) {
  static unsigned next = 0;
  const auto dir = std::filesystem::temp_directory_path() /
                   ("bm-malformed-kv-" + name + "-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(next++));
  std::filesystem::create_directories(dir);
  return dir;
}

echo::core::SessionInfo SyntheticSession() {
  echo::core::SessionInfo session;
  session.userId = "synthetic-user";
  session.token = "synthetic-token";
  return session;
}

std::optional<std::uint64_t> StoredGeneration(Database& db) {
  const auto payload = db.GetJson("session.info");
  if (!payload || !payload->is_object() || !payload->contains("gen") ||
      !(*payload)["gen"].is_number_unsigned()) {
    return std::nullopt;
  }
  return (*payload)["gen"].get<std::uint64_t>();
}

const json kMalformedDevices[] = {
    json::array(),
    json{{"guid", 42}, {"registered", "no"}},
    json{{"dfid", "real-dfid"}, {"mid", 5}, {"registered", true}},
    json("device-as-string"),
};

void DeviceRepositoryTreatsMalformedAsMissing() {
  for (const auto& malformed : kMalformedDevices) {
    Database db;
    db.Open(FreshDir("device-repo") / "bottlemusic.db");
    db.Initialize();
    db.SetJson("device.info", malformed);
    DeviceRepository repo(db);
    bool loadThrew = false;
    std::optional<echo::core::DeviceInfo> loaded;
    try {
      loaded = repo.Load();
    } catch (...) {
      loadThrew = true;
    }
    CHECK(!loadThrew, "malformed device.info does not throw out of Load");
    CHECK(!loaded.has_value(), "malformed device.info reads as no record");

    echo::core::DeviceService service(repo);
    const auto device = service.EnsureDeviceReady();
    CHECK(!device.guid.empty() && !device.registered,
          "a fresh unregistered identity replaces the malformed record");
    const auto reloaded = repo.Load();
    CHECK(reloaded && reloaded->guid == device.guid,
          "the replacement identity is persisted and readable");
    db.Close();
  }

  Database db;
  db.Open(FreshDir("device-repo-valid") / "bottlemusic.db");
  db.Initialize();
  db.SetJson("device.info", json{{"dfid", "kept-dfid"}, {"guid", "kept-guid"}, {"registered", true}});
  const auto valid = DeviceRepository(db).Load();
  CHECK(valid && valid->dfid == "kept-dfid" && valid->guid == "kept-guid" && valid->registered,
        "a well-formed device record still loads unchanged");
  db.Close();
  std::cout << "[MalformedKv] device_repository_malformed_records ok\n";
}

void SessionRepositoryHandlesMalformedEnvelopes() {
  // Wrong-typed version: not a session, never a throw; generation still
  // advances from the readable gen.
  {
    Database db;
    db.Open(FreshDir("session-version") / "bottlemusic.db");
    db.Initialize();
    db.SetJson("session.info", json{{"version", "oops"}, {"gen", 1}});
    SessionRepository repo(db);
    const auto snapshot = repo.LoadSnapshot();
    CHECK(!snapshot.session.has_value(), "wrong-typed version reads as no session");
    CHECK(repo.Save(SyntheticSession()) == 2, "Save advances from the readable generation");
    CHECK(repo.Load().has_value(), "a fresh login over the malformed envelope works");
    db.Close();
  }

  // Wrong-typed / negative / non-object generation: logout succeeds, and the
  // generation jumps past anything a pre-corruption request could hold.
  for (const auto& malformed : {json{{"version", 1}, {"gen", "oops"}},
                                json{{"version", 1}, {"gen", -5}},
                                json{{"version", 1}, {"gen", 1.5}},
                                json::array()}) {
    Database db;
    db.Open(FreshDir("session-gen") / "bottlemusic.db");
    db.Initialize();
    db.SetJson("session.info", malformed);
    SessionRepository repo(db);
    CHECK(!repo.LoadSnapshot().session.has_value(), "unreadable generation reads as no session");
    const auto before = static_cast<std::uint64_t>(SessionRepository::CurrentUnixTimeMs());
    bool clearThrew = false;
    try {
      repo.Clear();
    } catch (...) {
      clearThrew = true;
    }
    CHECK(!clearThrew, "logout over an unreadable generation succeeds");
    const auto cleared = StoredGeneration(db);
    CHECK(cleared && *cleared >= before, "logout moves the generation to the recovery floor");
    const auto next = repo.Save(SyntheticSession());
    CHECK(next == *cleared + 1, "the sequence continues normally from the floor");
    db.Close();
  }

  // A real protected session whose gen got corrupted: the old ticket can no
  // longer patch, and the next identity write cannot reuse the old value.
  {
    Database db;
    db.Open(FreshDir("session-corrupt-gen") / "bottlemusic.db");
    db.Initialize();
    SessionRepository repo(db);
    const auto ticket = repo.Save(SyntheticSession());
    auto payload = db.GetJson("session.info");
    CHECK(payload && payload->contains("protected_data"), "protected payload stored");
    (*payload)["gen"] = "corrupted";
    db.SetJson("session.info", *payload);

    CHECK(!repo.LoadSnapshot().session.has_value(),
          "a session with an unreadable generation is not exposed");
    CHECK(!repo.PatchIfGeneration(ticket, [](auto& s) { s.nickname = "late"; }),
          "a pre-corruption ticket cannot patch");
    const auto untouched = db.GetJson("session.info");
    CHECK(untouched && (*untouched)["gen"] == "corrupted", "a rejected patch writes nothing");
    const auto next = repo.Save(SyntheticSession());
    CHECK(next > ticket, "generation never moves backwards after corruption");
    CHECK(!repo.PatchIfGeneration(ticket, [](auto& s) { s.nickname = "late"; }),
          "the pre-corruption ticket stays rejected after re-login");
    db.Close();
  }

  // Wrong-typed fields in a legacy plaintext payload do not throw.
  {
    Database db;
    db.Open(FreshDir("session-legacy") / "bottlemusic.db");
    db.Initialize();
    db.SetJson("session.info", json{{"token", 5}, {"userid", json::array()}, {"vip_type", "x"}});
    bool threw = false;
    try {
      CHECK(!SessionRepository(db).Load().has_value(), "wrong-typed legacy fields read as empty");
    } catch (...) {
      threw = true;
    }
    CHECK(!threw, "wrong-typed legacy fields do not throw");
    db.Close();
  }
  std::cout << "[MalformedKv] session_repository_malformed_envelopes ok\n";
}

json Request(const char* method, const char* path, const char* query) {
  char* raw = nullptr;
  EchoHandleRequest(method, path, query, "{}", "", &raw);
  CHECK(raw != nullptr, "C API produced a response");
  const auto parsed = json::parse(raw, nullptr, false);
  EchoFreeString(raw);
  CHECK(!parsed.is_discarded() && parsed.is_object(), "C API response is a JSON object");
  return parsed;
}

void ExpectOk(const json& response, const char* what) {
  if (!response.contains("status") || response["status"] != 200) {
    std::fprintf(stderr, "[MalformedKv] %s -> %s\n", what, response.dump().c_str());
  }
  CHECK(response.contains("status") && response["status"] == 200, what);
}

// Same routes and payloads as the release audit's KV probe, through the
// exported C API. The records are written by a second connection while the
// native context is running, as an external writer or restored backup would.
void CompatRoutesRecoverThroughCApi() {
  struct Case {
    const char* key;
    json value;
  };
  const Case cases[] = {
      {"session.info", json{{"version", "oops"}, {"gen", 1}}},
      {"session.info", json{{"version", 1}, {"gen", "oops"}}},
      {"device.info", json::array()},
      {"device.info", json{{"guid", 42}, {"registered", "no"}}},
  };
  for (const auto& c : cases) {
    const auto dir = FreshDir("capi");
    CHECK(EchoInitializeWithPathsV2(dir.string().c_str()) == 0, "native context initializes");
    Database writer;
    writer.Open(dir / "bottlemusic.db");
    writer.Initialize();

    writer.SetJson(c.key, c.value);
    ExpectOk(Request("GET", "/user/detail", "{}"), "/user/detail over malformed record");
    const auto device = Request("GET", "/settings/device", "{}");
    ExpectOk(device, "/settings/device over malformed record");
    CHECK(device["body"]["data"]["guid"].is_string() &&
              !device["body"]["data"]["guid"].get<std::string>().empty(),
          "/settings/device returns a usable identity");

    writer.SetJson(c.key, c.value);
    const auto reset = Request("GET", "/settings/device", "{\"clear\":\"1\"}");
    ExpectOk(reset, "/settings/device?clear=1 over malformed record");
    CHECK(reset["body"]["updated"] == true, "explicit device reset reports the change");

    writer.SetJson(c.key, c.value);
    const auto logout = Request("POST", "/auth/logout", "{}");
    ExpectOk(logout, "/auth/logout over malformed record");
    CHECK(logout["body"]["data"]["cleared"] == true, "logout reports cleared");
    const auto session = writer.GetJson("session.info");
    CHECK(session && session->is_object() && !session->contains("protected_data"),
          "logout leaves only a session tombstone");
    const auto deviceAfter = writer.GetJson("device.info");
    CHECK(deviceAfter && deviceAfter->is_object() && deviceAfter->size() == 1 && deviceAfter->contains("_revision"),
          "logout clears device identity and retains the revision tombstone");

    writer.Close();
    CHECK(EchoShutdown() == 0, "native context shuts down");
  }
  std::cout << "[MalformedKv] compat_routes_recover_over_malformed_records ok\n";
}

}  // namespace

int main() {
  DeviceRepositoryTreatsMalformedAsMissing();
  SessionRepositoryHandlesMalformedEnvelopes();
  CompatRoutesRecoverThroughCApi();
  std::cout << "[MalformedKv] all ok\n";
  return 0;
}

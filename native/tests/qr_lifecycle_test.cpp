#include "echo/core/CompatRoutes.h"
#include "echo/core/HttpClient.h"
#include "echo/core/LoginService.h"
#include "echo/storage/SessionRepository.h"
#include "echo/storage/DeviceRepository.h"
#include <filesystem>
#include <future>
#include <iostream>
#include <chrono>
#include <stdexcept>

using namespace echo::core;
using namespace echo::storage;
using namespace std::chrono_literals;

void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
  std::cout << "PASS " << description << '\n';
}
int main() {
  const auto root = std::filesystem::temp_directory_path() / ("bottlemusic-qr-lifecycle-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  try {
    Database db;
    db.Open(root / "fixture.db");
    db.Initialize();
    SessionRepository sessions(db);
    DeviceRepository devices(db);
    {
      std::promise<void> entered, release;
      auto gate = release.get_future().share();
      auto pending = std::async(std::launch::async, [&] {
        return HandleLoginQrKey(db, [&](const DeviceInfo&) {
          entered.set_value();
          Check(gate.wait_for(3s) == std::future_status::ready, "key fixture gate released");
          return nlohmann::json{{"status", 1}, {"data", {{"qrcode", "old-key"}}}};
        }, {{"attempt_id", "old-attempt"}});
      });
      Check(entered.get_future().wait_for(2s) == std::future_status::ready, "key request reached fake upstream");
      const auto current = HandleLoginQrKey(db, [](const DeviceInfo&) {
        return nlohmann::json{{"status", 1}, {"data", {{"qrcode", "new-key"}}}};
      }, {{"attempt_id", "new-attempt"}});
      release.set_value();
      Check(pending.get().httpStatus == 409 && current.httpStatus == 200, "late key response cannot replace current key");
      Check(sessions.IsLoginAttemptCurrent(sessions.LoadSnapshot().generation, "new-key"), "current key ownership preserved");
      HandleLoginQrCancel(db, {{"attempt_id", "old-attempt"}});
      Check(sessions.IsLoginAttemptCurrent(sessions.LoadSnapshot().generation, "new-key"), "old page cancellation cannot cancel new page");
    }
    {
      sessions.SetActiveLoginKey("registration-key");
      // Fake registration waits while the real logout route clears both records.
      std::promise<void> entered, release;
      auto gate = release.get_future().share();
      auto pending = std::async(std::launch::async, [&] {
        return HandleLoginQrCheck(db, {{"key", "registration-key"}},
            [](const DeviceInfo&, std::string) {
              return nlohmann::json{{"status", 1}, {"data", {{"status", 4},
                  {"userid", "fixture-user"}, {"token", "fixture-token"}, {"vip_token", "fixture-vip"}}}};
            }, {}, {}, [&](const DeviceInfo&, const std::string&, const std::string&, std::string*) {
              entered.set_value();
              Check(gate.wait_for(3s) == std::future_status::ready, "registration fixture gate released");
              return std::string("fixture-registered-dfid");
            });
      });
      Check(entered.get_future().wait_for(2s) == std::future_status::ready, "registration reached fake upstream");
      Check(HandleAuthLogout(db).httpStatus == 200, "logout completes while registration is pending");
      release.set_value();
      Check(pending.get().httpStatus == 409, "late registration login result rejected");
      const auto tombstone = db.GetJson("device.info");
      Check(!sessions.Load() && tombstone && tombstone->size() == 1 && tombstone->contains("_revision"),
          "logout keeps device identity cleared and retains its revision");
    }
    {
      int upstreamCalls = 0;
      const auto rejected = HandleLoginQrCheck(db, {{"key", "never-issued"}}, [&](const DeviceInfo&, std::string) {
        ++upstreamCalls;
        return nlohmann::json::object();
      });
      Check(rejected.httpStatus == 409 && upstreamCalls == 0, "unregistered attempt sends no upstream request");
      sessions.SetActiveLoginKey("atomic-key");
      const auto cancelledAttempt = sessions.BeginLoginAttempt("cancelled-publication");
      Check(!sessions.PublishLoginKey(cancelledAttempt, "expired-key", [] { return false; }), "cancelled key publication cannot reach storage");
      sessions.SetActiveLoginKey("atomic-key");
      const auto gen = sessions.LoadSnapshot().generation;
      const auto beforeSession = db.GetJson("session.info");
      const auto beforeDevice = db.GetJson("device.info");
      db.Execute("CREATE TRIGGER fail_session BEFORE UPDATE ON kv_store WHEN NEW.key='session.info' BEGIN SELECT RAISE(ABORT,'fixture_pair_write_failure'); END;");
      SessionInfo session;
      session.userId = "fixture-user";
      session.token = "fixture-token";
      DeviceInfo device;
      device.dfid = "fixture-new-device";
      bool failed = false;
      try { sessions.CommitLoginAttempt(gen, "atomic-key", session, device); }
      catch (...) { failed = true; }
      Check(failed && beforeSession == db.GetJson("session.info") && beforeDevice == db.GetJson("device.info"), "failed session write rolls back companion device write");
      db.Execute("DROP TRIGGER fail_session;");
      Check(!sessions.CommitLoginAttempt(gen, "atomic-key", session, device, [] { return false; }), "cancelled commit guard prevents both writes");
      Check(sessions.CommitLoginAttempt(gen, "atomic-key", session, device), "valid current attempt commits session and device");
      Check(sessions.Load()->userId == "fixture-user" && devices.Load()->dfid == "fixture-new-device", "atomic commit persists matching device and session");
    }
    {
      // The retry policy belongs to the operation: polling asks again by nature,
      // channel creation must not (a replay would mint a second QR key). The two
      // seams are what keep that distinction out of the transport call.
      int channelCalls = 0;
      int pollCalls = 0;
      const auto reply = [](const char* body) {
        HttpResult result;
        result.statusCode = 200;
        result.body = body;
        return result;
      };
      LoginService service(
          [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
            ++channelCalls;
            return reply(R"({"status":1,"data":{"qrcode":"fixture-channel-key"}})");
          },
          [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
            ++pollCalls;
            return reply(R"({"status":1,"data":{"status":0}})");
          },
          [](const std::string&, const std::string&,
             const std::unordered_map<std::string, std::string>&) { return HttpResult{}; });
      const auto begin = service.BeginQrLogin(DeviceInfo{});
      const auto poll = service.PollQrLogin(DeviceInfo{}, "fixture-channel-key");
      Check(channelCalls == 1 && pollCalls == 1, "channel creation and polling each use their own GET seam");
      Check(begin["data"]["qrcode"] == "fixture-channel-key" && poll["status"] == 1,
            "both seams still produce the caller-visible payload");
    }
    {
      bool channelRetry = true;
      bool pollRetry = false;
      const auto reply = [](const char* body) {
        HttpResult result;
        result.statusCode = 200;
        result.body = body;
        return result;
      };
      LoginService retryAware(
          [&](const std::string&, const std::unordered_map<std::string, std::string>&, bool retryAllowed) {
            channelRetry = retryAllowed;
            return reply(R"({"status":1,"data":{"qrcode":"retry-policy-key"}})");
          },
          [&](const std::string&, const std::unordered_map<std::string, std::string>&, bool retryAllowed) {
            pollRetry = retryAllowed;
            return reply(R"({"status":1,"data":{"status":0}})");
          },
          [](const std::string&, const std::string&,
             const std::unordered_map<std::string, std::string>&) { return HttpResult{}; });
      retryAware.BeginQrLogin(DeviceInfo{});
      retryAware.PollQrLogin(DeviceInfo{}, "retry-policy-key");
      Check(!channelRetry && pollRetry,
            "QR channel creation is single-shot while read-only polling allows retries");
    }
    {
      int legacyCalls = 0;
      const LoginHttpGet legacyGet = [&](const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        ++legacyCalls;
        HttpResult result;
        result.statusCode = 200;
        result.body = R"({"status":1,"data":{"qrcode":"legacy-key"}})";
        return result;
      };
      LoginService legacy(legacyGet);
      legacy.BeginQrLogin(DeviceInfo{});
      legacy.PollQrLogin(DeviceInfo{}, "legacy-key");
      Check(legacyCalls == 2, "legacy GET constructor still shares one handler across create and poll");

      const LoginHttpPost legacyPost = [](const std::string&, const std::string&,
          const std::unordered_map<std::string, std::string>&) { return HttpResult{}; };
      LoginService legacyWithPost(legacyGet, legacyPost);
      legacyWithPost.BeginQrLogin(DeviceInfo{});
      legacyWithPost.PollQrLogin(DeviceInfo{}, "legacy-key");
      Check(legacyCalls == 4, "legacy GET/POST constructor still shares its GET handler across create and poll");
    }
    {
      // The scan succeeded, but registration outran the route budget. Whatever
      // the guard reports, it must not look like a successful login and it must
      // not swap the stored session for the one it could not commit.
      const auto committedUser = sessions.Load() ? sessions.Load()->userId : std::string{};
      const auto committedToken = sessions.Load() ? sessions.Load()->token : std::string{};
      sessions.SetActiveLoginKey("budget-key");
      echo::core::HttpClientCancellationScope budget(nullptr, 150);
      const auto timedOut = HandleLoginQrCheck(db, {{"key", "budget-key"}},
          [](const DeviceInfo&, std::string) {
            return nlohmann::json{{"status", 1}, {"data", {{"status", 4},
                {"userid", "fixture-user"}, {"token", "fixture-token"}}}};
          }, {}, {}, [](const DeviceInfo&, const std::string&, const std::string&, std::string*) {
            // Stands in for a slow registration endpoint: it must spend the
            // budget before the session write is attempted.
            std::this_thread::sleep_for(300ms);
            return std::string("fixture-late-dfid");
          });
      const auto reason = timedOut.body.value("error_code", timedOut.body.value("error", std::string{}));
      Check(timedOut.httpStatus != 200, "an out-of-budget commit never reports success");
      Check(timedOut.httpStatus == 504 && reason == "login_commit_timeout",
            "an out-of-budget commit reports its specific timeout contract");
      Check(sessions.Load() && sessions.Load()->userId == committedUser
                && sessions.Load()->token == committedToken,
            "an out-of-budget commit does not swap the stored session");
      std::cout << "  [info] out-of-budget commit reported as " << reason
                << " (status " << timedOut.httpStatus << ")" << std::endl;
    }
    db.Close();
    std::filesystem::remove_all(root); // unique fixture directory created above
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}

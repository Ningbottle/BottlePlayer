#include "echo/core/CompatRoutes.h"
#include "echo/storage/Database.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"
#include "echo/core/DeviceService.h"
#include "echo/storage/IdentityCodec.h"
#include <barrier>
#include <future>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using echo::core::DeviceInfo;
using echo::core::SessionInfo;
using nlohmann::json;

static void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  try {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("bottlemusic-decoupling-audit-" + std::to_string(nonce) + ".db");
    echo::storage::Database db;
    db.Open(path);
    db.Initialize();
    echo::storage::SessionRepository sessions(db);
    echo::storage::DeviceRepository devices(db);
    SessionInfo account;
    account.userId = "12345";
    account.token = "synthetic-audit-token";
    DeviceInfo original;
    original.dfid = "-";
    original.guid = "synthetic-audit-guid";
    original.uuid = "synthetic-audit-uuid";
    original.mid = "12345";
    original.appid = "3116";
    original.clientver = "11440";
    original.registered = false;

    const auto playlists = [](const DeviceInfo&, std::string, std::string, int, int) {
      return json{{"status", 1}, {"data", {{"list", json::array()}, {"total", 0}}}};
    };
    json evidence;

    sessions.Save(account);
    devices.Save(original);
    bool registrationReached = false;
    echo::core::HandleUserPlaylist(db, {}, playlists,
        [&](const DeviceInfo&, std::string, std::string, std::string*) {
          registrationReached = true;
          // Exact ordering: capture old identity -> logout completes -> old
          // upstream registration returns. No network and no timing sleeps.
          echo::core::HandleAuthLogout(db);
          return std::string("synthetic-late-dfid");
        });
    const auto afterLogout = sessions.LoadSnapshot();
    const auto deviceAfterLogout = devices.Load();
    Require(registrationReached, "registration seam was not reached");
    const bool resurrected = deviceAfterLogout && deviceAfterLogout->registered &&
        deviceAfterLogout->dfid == "synthetic-late-dfid";
    Require(!afterLogout.session && !resurrected, "late registration restored logged-out device");
    evidence["late_registration_after_logout"] = {
        {"session_absent", !afterLogout.session.has_value()},
        {"old_device_restored", resurrected}};

    sessions.Save(account);
    devices.Save(original);
    const auto beforeOverride = sessions.LoadSnapshot();
    DeviceInfo overridden = original;
    overridden.guid = "synthetic-new-guid";
    overridden.dfid = "synthetic-manual-dfid";
    overridden.registered = true;
    echo::core::HandleUserPlaylist(db, {}, playlists,
        [&](const DeviceInfo&, std::string, std::string, std::string*) {
          devices.Save(overridden);
          return std::string("synthetic-late-dfid");
        });
    const auto afterOverride = devices.Load();
    const bool overwritten = afterOverride && afterOverride->guid == original.guid &&
        afterOverride->dfid == "synthetic-late-dfid";
    Require(!overwritten && afterOverride && afterOverride->guid == overridden.guid && afterOverride->dfid == overridden.dfid, "late registration overwrote manually replaced device");
    evidence["late_registration_after_device_override"] = {
        {"same_account_generation", beforeOverride.generation == sessions.LoadSnapshot().generation},
        {"new_device_overwritten", overwritten}};

    sessions.Save(account);
    devices.Save(original);
    const auto attempt = sessions.BeginLoginAttempt("synthetic-audit-attempt");
    Require(sessions.PublishLoginKey(attempt, "synthetic-audit-key"), "cannot publish fake QR key");
    const auto ticket = sessions.LoadSnapshot();
    echo::core::HandleAuthLogout(db);
    const bool accepted = sessions.CommitLoginAttempt(
        ticket.generation, "synthetic-audit-key", account, overridden);
    const auto qrDevice = devices.Load();
    Require(!accepted && !sessions.Load() && qrDevice && !qrDevice->registered &&
        qrDevice->dfid.empty(), "QR stale commit control failed");
    evidence["qr_commit_after_logout_control"] = {
        {"commit_rejected", !accepted}, {"session_and_device_remain_cleared", true}};

    // A second Actor/connection must observe the same transactional protocol.
    echo::storage::Database other;
    other.Open(path); other.Initialize();
    echo::storage::SessionRepository peer(other);
    sessions.Save(account); devices.Save(original);
    auto captured = sessions.CaptureIdentity();
    echo::storage::DeviceRepository(other).Save(overridden);
    echo::execution::OperationContext operation;
    Require(sessions.CommitRegistration(captured.version, {"late"}, operation) ==
        echo::model::CommitResult::StaleDevice, "cross-connection device revision ignored");
    captured = sessions.CaptureIdentity();
    peer.ClearWithDevice();
    Require(sessions.CommitRegistration(captured.version, {"late"}, operation) ==
        echo::model::CommitResult::StaleAccount, "cross-connection account generation ignored");

    // Two first requests cannot each install a different identity.
    std::barrier start(2);
    auto ensure = [&](echo::storage::Database& connection) {
      echo::storage::DeviceRepository repository(connection);
      start.arrive_and_wait();
      return echo::core::DeviceService(repository).EnsureDeviceReady();
    };
    auto first = std::async(std::launch::async, [&] { return ensure(db); });
    auto second = std::async(std::launch::async, [&] { return ensure(other); });
    Require(first.get().guid == second.get().guid, "two initializers installed different devices");

    // Cancellation before admission refuses all identity payload writes.
    sessions.Save(account); devices.Save(original);
    captured = sessions.CaptureIdentity();
    echo::execution::OperationContext cancelled;
    cancelled.stop->Cancel();
    Require(sessions.CommitRegistration(captured.version, {"cancelled"}, cancelled) ==
        echo::model::CommitResult::CancelledBeforeCommit, "cancelled registration committed");
    echo::execution::OperationContext expired;
    expired.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    Require(sessions.CommitRegistration(captured.version, {"expired"}, expired) ==
        echo::model::CommitResult::DeadlineBeforeCommit, "expired registration committed");
    echo::model::CredentialBundle bundle{"new-token", "new-vip", "new-t1", 1};
    echo::storage::DeviceRepository(other).Save(overridden);
    Require(!sessions.PatchIdentity(captured.version, nullptr, &bundle, operation),
        "refresh on replaced device committed credentials");

    // QR must also validate the identity used for registration/refresh.
    const auto qrAttempt = sessions.BeginLoginAttempt("device-version-control");
    Require(sessions.PublishLoginKey(qrAttempt, "device-version-key"), "QR setup failed");
    captured = sessions.CaptureIdentity();
    echo::storage::DeviceRepository(other).Save(original);
    Require(!sessions.CommitLoginAttempt(captured.version.accountGeneration, "device-version-key", account,
        overridden, {}, captured.version.deviceRevision), "QR used stale device version");

    // Commit admission precedes cancellation: once authorized, the accepted
    // transaction finishes atomically even if cancellation arrives at that seam.
    captured = sessions.CaptureIdentity();
    Require(sessions.CommitLoginAttempt(captured.version.accountGeneration, "device-version-key", account,
        overridden, [&] { const bool permit = operation.TryCommit(); operation.stop->Cancel(); return permit; },
        captured.version.deviceRevision), "accepted commit was revoked by later cancellation");

#if defined(ECHO_NATIVE_HAS_SQLITE)
    // Inject a failure in the marker write: the protected payload must roll back
    // with it. This catches non-transactional plaintext migrations.
    db.SetJson("session.encryption_migrated", false);
    db.SetJson("session.info", echo::storage::EncodeSession(account));
    db.Execute("CREATE TRIGGER fail_session_migration BEFORE UPDATE ON kv_store WHEN NEW.key='session.encryption_migrated' BEGIN SELECT RAISE(ABORT, 'synthetic migration failure'); END;");
    bool migrationFailed = false;
    try { sessions.LoadSnapshot(); } catch (...) { migrationFailed = true; }
    Require(migrationFailed && db.GetJson("session.info")->contains("token"), "migration partially committed");
    db.Execute("DROP TRIGGER fail_session_migration;");
    Require(sessions.LoadSnapshot().session.has_value(), "legacy session could not migrate");
    peer.ClearWithDevice();
    Require(!sessions.LoadSnapshot().session, "migration restored logout");
#endif
    sessions.Save(account); devices.Save(original);
    const auto directRegistration = echo::core::HandleRegisterDev(db, {}, {},
        [&](const DeviceInfo&, std::string, std::string, std::string*) {
          peer.ClearWithDevice();
          return "direct-late-dfid";
        });
    Require(directRegistration.httpStatus == 409 && !sessions.Load() && devices.Load()->guid.empty(),
        "standalone registration route restored logged-out identity");
    sessions.Save(account); devices.Save(original);
    const auto epochTicket = sessions.CaptureIdentity();
    const auto epochAttempt = sessions.BeginLoginAttempt("old-process-attempt");
    Require(sessions.PublishLoginKey(epochAttempt, "old-process-key"), "epoch QR setup failed");
    db.SetJson("unrelated.local.data", true);
    sessions.BeginRuntimeEpoch();
    const auto freshEpoch = sessions.CaptureIdentity();
    Require(freshEpoch.session && freshEpoch.session->token == account.token &&
        freshEpoch.version.accountGeneration > epochTicket.version.accountGeneration &&
        freshEpoch.version.deviceRevision > epochTicket.version.deviceRevision &&
        !sessions.IsLoginAttemptCurrent(freshEpoch.version.accountGeneration, "old-process-key") &&
        db.GetJson("unrelated.local.data") == true, "startup epoch did not preserve data and invalidate old tickets");
    other.Close();
    evidence["identity_protocol"] = {{"cross_connection", true}, {"concurrent_initialization", true},
        {"cancel_and_deadline", true}, {"qr_device_version", true}, {"migration_rollback", true}};

    db.Close();
    // Delete only the uniquely named synthetic database owned by this probe.
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.wstring() + L"-wal", ignored);
    std::filesystem::remove(path.wstring() + L"-shm", ignored);
    std::cout << evidence.dump(2) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

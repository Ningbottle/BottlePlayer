#include "echo/storage/SessionRepository.h"

#include "echo/storage/IdentityCodec.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include "echo/storage/SecretStore.h"
#include <openssl/crypto.h>
#include <unistd.h>
#define SecureZeroMemory OPENSSL_cleanse
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace model = echo::model;
using echo::storage::DecodeSession;

constexpr int kProtectedSessionVersion = 1;
#ifdef _WIN32
constexpr DWORD kBase64EncodeFlags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;

std::string Base64Encode(const BYTE* data, DWORD size) {
  DWORD encodedSize = 0;
  if (!CryptBinaryToStringA(data, size, kBase64EncodeFlags, nullptr, &encodedSize)) {
    throw std::runtime_error("session_base64_encode_failed");
  }

  std::string encoded(encodedSize, '\0');
  if (!CryptBinaryToStringA(data, size, kBase64EncodeFlags, encoded.data(), &encodedSize)) {
    throw std::runtime_error("session_base64_encode_failed");
  }
  if (encodedSize > 0 && encoded[encodedSize - 1] == '\0') {
    --encodedSize;
  }
  encoded.resize(encodedSize);
  return encoded;
}

std::optional<std::vector<BYTE>> Base64Decode(const std::string& encoded) {
  DWORD decodedSize = 0;
  if (!CryptStringToBinaryA(encoded.c_str(), 0, CRYPT_STRING_BASE64, nullptr,
                            &decodedSize, nullptr, nullptr)) {
    return std::nullopt;
  }

  std::vector<BYTE> decoded(decodedSize);
  if (!CryptStringToBinaryA(encoded.c_str(), 0, CRYPT_STRING_BASE64, decoded.data(),
                            &decodedSize, nullptr, nullptr)) {
    return std::nullopt;
  }
  decoded.resize(decodedSize);
  return decoded;
}

std::string ProtectForCurrentUser(const std::string& plaintext) {
  DATA_BLOB input{
      static_cast<DWORD>(plaintext.size()),
      reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data())),
  };
  DATA_BLOB output{};
  if (!CryptProtectData(&input, L"BottleMusic account session", nullptr, nullptr,
                        nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    throw std::runtime_error("session_protection_failed");
  }

  try {
    const auto encoded = Base64Encode(output.pbData, output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encoded;
  } catch (...) {
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    throw;
  }
}

std::optional<std::string> UnprotectForCurrentUser(const std::string& encoded) {
  const auto decoded = Base64Decode(encoded);
  if (!decoded || decoded->empty()) return std::nullopt;

  DATA_BLOB input{
      static_cast<DWORD>(decoded->size()),
      const_cast<BYTE*>(decoded->data()),
  };
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    return std::nullopt;
  }

  std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return plaintext;
}
#else
using echo::storage::ProtectForCurrentUser;
using echo::storage::UnprotectForCurrentUser;
#endif

bool IsEmptySession(const model::SessionInfo& session) {
  return session.token.empty() && session.userId.empty() && session.t1.empty() &&
         session.nickname.empty() && session.pic.empty();
}

struct PlaintextWiper {
  std::string& plaintext;
  ~PlaintextWiper() {
    if (!plaintext.empty()) {
      SecureZeroMemory(plaintext.data(), plaintext.size());
    }
  }
};

// B02: the account generation lives in the PLAIN part of the session payload
// (it is not a secret). Legacy payloads without it read as generation 0; the
// first authoritative Save stamps 1.
constexpr const char* kSessionKey = "session.info";
constexpr const char* kLazyVipRefreshKey = "lazy_vip_refresh";
constexpr const char* kActiveLoginKey = "active_login_key";
constexpr std::int64_t kLazyVipRefreshCooldownMs = 30'000;

std::optional<std::uint64_t> ReadGeneration(const nlohmann::json& payload) {
  if (!payload.is_object() || !payload.contains("gen")) return std::uint64_t{0};
  const auto& value = payload["gen"];
  try {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
      const auto signedValue = value.get<std::int64_t>();
      if (signedValue >= 0) return static_cast<std::uint64_t>(signedValue);
    }
  } catch (const nlohmann::json::exception&) {
  }
  return std::nullopt;
}

// Legacy plaintext payloads are bare SessionInfo JSON. Anything carrying an
// envelope key was written by the protected format and must never be
// reinterpreted as plaintext credentials, even when it is malformed.
bool IsSessionEnvelope(const nlohmann::json& payload) {
  return payload.contains("version") || payload.contains("gen") ||
         payload.contains("protected_data");
}

std::uint64_t CurrentUnixTimeMsOrZero() {
  const auto now = echo::storage::SessionRepository::CurrentUnixTimeMs();
  return now > 0 ? static_cast<std::uint64_t>(now) : 0;
}

// The generation every identity-changing write (Save / Clear) stamps.
// An absent record or a legacy payload without "gen" starts the sequence at
// 1. When a record exists but its generation cannot be read (wrong type,
// negative, non-object payload), the value it used to hold is unknown and
// an in-flight request may still carry it, so restarting at 1 could hand a
// stale writer a matching generation. Jump to wall-clock milliseconds
// instead: increments of one per login/logout never come near it, so the
// sequence keeps moving forward past anything previously issued.
std::uint64_t NextGeneration(const std::optional<nlohmann::json>& current) {
  if (!current) return 1;
  if (current->is_object()) {
    const auto generation = ReadGeneration(*current);
    if (generation && *generation < std::numeric_limits<std::uint64_t>::max()) {
      return *generation + 1;
    }
  }
  ECHO_LOG("SessionRepository",
           "unreadable session generation -> advancing to recovery floor");
  return std::max<std::uint64_t>(CurrentUnixTimeMsOrZero(), 1);
}

std::optional<std::int64_t> ReadUnixMilliseconds(const nlohmann::json& value) {
  try {
    if (value.is_number_unsigned()) {
      const auto unsignedValue = value.get<std::uint64_t>();
      if (unsignedValue <= static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(unsignedValue);
      }
      return std::nullopt;
    }
    if (value.is_number_integer()) return value.get<std::int64_t>();
  } catch (const nlohmann::json::exception&) {
  }
  return std::nullopt;
}

std::optional<model::SessionInfo> ReadProtectedSession(
    const nlohmann::json& payload) {
  if (!payload.is_object() || !payload.contains("version") ||
      !payload["version"].is_number_integer() ||
      payload["version"].get<int>() != kProtectedSessionVersion ||
      !payload.contains("protected_data") ||
      !payload["protected_data"].is_string()) {
    return std::nullopt;
  }
  auto plaintext = UnprotectForCurrentUser(
      payload["protected_data"].get<std::string>());
  if (!plaintext) return std::nullopt;
  PlaintextWiper wipe{*plaintext};
  try {
    return DecodeSession(nlohmann::json::parse(*plaintext));
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

bool IsLazyVipRefreshSuppressed(const nlohmann::json& metadata,
                                const std::string& processEpoch,
                                std::int64_t nowUnixMs) {
  if (!metadata.is_object()) return false;

  const auto inFlight = metadata.find("in_flight");
  const auto epoch = metadata.find("epoch");
  const auto claimId = metadata.find("claim_id");
  if (inFlight != metadata.end() && inFlight->is_boolean() &&
      inFlight->get<bool>()) {
    // Only a well-formed claim from this process can still be live. A foreign
    // process (or malformed metadata) is an abandoned claim and may be taken
    // over immediately after a crash/restart.
    if (epoch != metadata.end() && epoch->is_string() &&
        epoch->get<std::string>() == processEpoch &&
        claimId != metadata.end() && claimId->is_string() &&
        !claimId->get<std::string>().empty()) {
      return true;
    }
    return false;
  }

  const auto retryAt = metadata.find("retry_after_ms");
  if (retryAt == metadata.end() || nowUnixMs < 0) return false;
  const auto retryUnixMs = ReadUnixMilliseconds(*retryAt);
  if (!retryUnixMs || *retryUnixMs <= nowUnixMs) return false;
  // A future time farther away than the bounded cooldown indicates clock
  // rollback or malformed state. Treat it as expired so it cannot suppress
  // refresh indefinitely.
  return *retryUnixMs - nowUnixMs <= kLazyVipRefreshCooldownMs;
}

std::string NextLazyVipClaimId(const std::string& processEpoch) {
  static std::atomic<std::uint64_t> nextSequence{1};
  return processEpoch + ":" +
         std::to_string(nextSequence.fetch_add(1, std::memory_order_relaxed));
}

std::int64_t CooldownDeadline(std::int64_t completedUnixMs) {
  if (completedUnixMs < 0) return completedUnixMs;
  if (completedUnixMs > std::numeric_limits<std::int64_t>::max() -
                            kLazyVipRefreshCooldownMs) {
    return std::numeric_limits<std::int64_t>::max();
  }
  return completedUnixMs + kLazyVipRefreshCooldownMs;
}

}  // namespace

namespace echo::storage {

SessionRepository::SessionRepository(Database& database) : database_(database) {}

SessionSnapshot SessionRepository::LoadSnapshot() {
  SessionSnapshot view;
  // Migration reads, encryption and marker update use one transaction. A late
  // plaintext reader can never replace a newer login/logout on another connection.
  database_.UpdateJsonPair(kSessionKey, "session.encryption_migrated",
      [&](auto payload, auto migrated) -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    if (!payload || !payload->is_object() || payload->empty()) return std::nullopt;
    if (IsSessionEnvelope(*payload)) {
      const auto generation = ReadGeneration(*payload);
      if (!generation) return std::nullopt;
      view.generation = *generation;
      const auto session = ReadProtectedSession(*payload);
      if (session && !IsEmptySession(*session)) view.session = *session;
      return std::nullopt;
    }
    if (migrated && *migrated == true) return std::nullopt;
    const auto session = DecodeSession(*payload);
    if (IsEmptySession(session)) return std::nullopt;
    view.generation = NextGeneration(payload);
    view.session = session;
    auto plaintext = EncodeSession(session).dump();
    PlaintextWiper wipe{plaintext};
    nlohmann::json protectedPayload{{"version", kProtectedSessionVersion},
        {"gen", view.generation}, {"protected_data", ProtectForCurrentUser(plaintext)}};
    for (const auto* key : {kActiveLoginKey, "active_login_attempt"})
      if (payload->contains(key)) protectedPayload[key] = (*payload)[key];
    return std::pair{std::move(protectedPayload), nlohmann::json(true)};
  });
  return view;
}

model::IdentitySnapshot SessionRepository::CaptureIdentity() {
  return CaptureIdentity({});
}

model::IdentitySnapshot SessionRepository::CaptureIdentity(
    const std::function<model::DeviceInfo(std::optional<model::DeviceInfo>)>& initialize,
    std::optional<std::uint64_t> expectedAccount) {
  LoadSnapshot(); // complete legacy migration before taking the atomic pair
  model::IdentitySnapshot view;
  database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto session, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    bool repaired = false;
    if (session) {
      const auto generation = ReadGeneration(*session);
      if (generation) {
        view.version.accountGeneration = *generation;
        view.session = ReadProtectedSession(*session);
      } else if (initialize && !expectedAccount) {
        // An unreadable generation cannot authorize a ticket. Repair the
        // invalid account as an anonymous tombstone before local device use.
        view.version.accountGeneration = NextGeneration(session);
        session = nlohmann::json{{"version", kProtectedSessionVersion}, {"gen", view.version.accountGeneration}};
        repaired = true;
      }
      if (view.session && IsEmptySession(*view.session)) view.session.reset();
    }
    view.version.deviceRevision = DeviceRevision(device);
    const auto existing = device && IsWellFormedDeviceRecord(*device) ? std::optional{DecodeDevice(*device)} : std::nullopt;
    if (existing) view.device = *existing;
    if (initialize && (!expectedAccount || *expectedAccount == view.version.accountGeneration)) {
      view.device = initialize(existing);
      auto payload = EncodeDevice(view.device);
      if (!existing || payload != EncodeDevice(*existing)) {
        payload = StampDevice(std::move(payload), device);
        ++view.version.deviceRevision;
        return std::pair{session.value_or(nlohmann::json::object()), std::move(payload)};
      }
    }
    if (repaired) return std::pair{*session, device.value_or(nlohmann::json::object())};
    return std::nullopt;
  });
  return view;
}

model::CommitResult SessionRepository::CommitRegistration(model::IdentityVersion expected,
    const model::RegisteredDevicePatch& patch, const execution::OperationContext& operation) {
  auto result = model::CommitResult::StaleAccount;
  if (patch.dfid.empty()) throw std::invalid_argument("empty_registered_dfid");
  database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto session, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    const auto generation = session ? ReadGeneration(*session) : std::optional<std::uint64_t>{0};
    if (!generation || *generation != expected.accountGeneration) return std::nullopt;
    result = model::CommitResult::StaleDevice;
    if (DeviceRevision(device) != expected.deviceRevision) return std::nullopt;
    auto updated = device ? DecodeDevice(*device) : model::DeviceInfo{};
    updated.dfid = patch.dfid;
    updated.registered = true;
    auto payload = StampDevice(EncodeDevice(updated), device);
    if (!operation.TryCommit()) {
      result = operation.Reason() == execution::StopReason::Deadline ? model::CommitResult::DeadlineBeforeCommit : model::CommitResult::CancelledBeforeCommit;
      return std::nullopt;
    }
    result = model::CommitResult::Applied;
    return std::pair{session.value_or(nlohmann::json::object()), std::move(payload)};
  });
  return result;
}

std::optional<model::SessionInfo> SessionRepository::PatchIdentity(model::IdentityVersion expected,
    const model::ProfilePatch* profile, const model::CredentialBundle* credentials,
    const execution::OperationContext& operation) {
  std::optional<model::SessionInfo> updated;
  database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto session, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    if (!session || ReadGeneration(*session) != expected.accountGeneration ||
        (credentials && DeviceRevision(device) != expected.deviceRevision)) return std::nullopt;
    auto current = ReadProtectedSession(*session);
    if (!current || IsEmptySession(*current)) return std::nullopt;
    if (profile) {
      if (!profile->nickname.empty()) current->nickname = profile->nickname;
      if (!profile->pic.empty()) current->pic = profile->pic;
    }
    if (credentials) {
      current->token = credentials->token; current->vipToken = credentials->vipToken;
      current->t1 = credentials->t1; current->vipType = credentials->vipType;
    }
    auto plaintext = EncodeSession(*current).dump();
    PlaintextWiper wipe{plaintext};
    (*session)["protected_data"] = ProtectForCurrentUser(plaintext);
    if (!operation.TryCommit()) return std::nullopt;
    updated = *current;
    return std::pair{*session, device.value_or(nlohmann::json::object())};
  });
  return updated;
}

std::optional<model::SessionInfo> SessionRepository::Load() {
  return LoadSnapshot().session;
}

void SessionRepository::BeginRuntimeEpoch() {
  LoadSnapshot();
  database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto session, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    const bool readable = session && ReadGeneration(*session) && ReadProtectedSession(*session).has_value();
    auto payload = readable ? *session : nlohmann::json::object();
    payload["version"] = kProtectedSessionVersion;
    payload["gen"] = NextGeneration(session);
    payload.erase(kActiveLoginKey);
    payload.erase("active_login_attempt");
    return std::pair{std::move(payload), StampDevice(device && device->is_object() ? *device : nlohmann::json::object(), device)};
  });
}

std::uint64_t SessionRepository::Save(const model::SessionInfo& session) {
  // Identity-changing write: always wins, and always advances the account
  // generation so every in-flight request that captured the previous
  // generation becomes a rejected stale writer (B02).
  std::uint64_t generation = 0;
  database_.UpdateJson(
      kSessionKey,
      [&session, &generation](std::optional<nlohmann::json> current)
          -> std::optional<nlohmann::json> {
        generation = NextGeneration(current);
        auto plaintext = EncodeSession(session).dump();
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", generation},
                                  {"protected_data", ProtectForCurrentUser(plaintext)}};
        SecureZeroMemory(plaintext.data(), plaintext.size());
        return payload;
      });
  return generation;
}

void SessionRepository::Clear() {
  // Logout: keep the generation sequence advancing (an empty payload still
  // carries gen so late writers of the ended session are rejected), drop the
  // protected credentials.
  database_.UpdateJson(
      kSessionKey,
      [](std::optional<nlohmann::json> current) -> std::optional<nlohmann::json> {
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", NextGeneration(current)}};
        return payload;
      });
}

void SessionRepository::ClearWithDevice() {
  database_.UpdateJsonPair(kSessionKey, "device.info",
      [](std::optional<nlohmann::json> current, std::optional<nlohmann::json> device) {
        return std::optional<std::pair<nlohmann::json, nlohmann::json>>{{
            {{"version", kProtectedSessionVersion}, {"gen", NextGeneration(current)}},
            StampDevice(nlohmann::json::object(), device)}};
      });
}

bool SessionRepository::PatchIfGeneration(
    std::uint64_t expectedGeneration,
    const std::function<void(model::SessionInfo&)>& mutator) {
  if (!mutator) return false;
  // Read, generation check, decrypt, patch, re-encrypt, and write all run as
  // ONE database actor operation — the conditional commit has no window for
  // an interleaved login/logout to slip through (B02).
  bool applied = false;
  database_.UpdateJson(
      kSessionKey,
      [&](std::optional<nlohmann::json> current) -> std::optional<nlohmann::json> {
        applied = false;
        if (!current || !current->is_object() || current->empty()) return std::nullopt;
        const auto generation = ReadGeneration(*current);
        if (!generation || *generation != expectedGeneration) {
          // The account moved on (logout / another account / re-login) while
          // this request was in flight, or the generation is unreadable.
          // Reject the late writer.
          return std::nullopt;
        }
        // Cannot read the stored session (legacy/anomalous payload) — never
        // overwrite it with a patch derived from a stale snapshot.
        auto stored = ReadProtectedSession(*current);
        if (!stored || IsEmptySession(*stored)) return std::nullopt;
        model::SessionInfo session = std::move(*stored);

        mutator(session);
        applied = true;

        auto updated = EncodeSession(session).dump();
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", expectedGeneration},
                                  {"protected_data", ProtectForCurrentUser(updated)}};
        // Same-generation profile/credential patches must not erase the
        // out-of-process claim and cooldown metadata attached to session.info.
        if (current->contains(kLazyVipRefreshKey)) {
          payload[kLazyVipRefreshKey] = (*current)[kLazyVipRefreshKey];
        }
        for (const auto* key : {kActiveLoginKey, "active_login_attempt"}) {
          if (current->contains(key)) payload[key] = (*current)[key];
        }
        SecureZeroMemory(updated.data(), updated.size());
        return payload;
      });
  return applied;
}

void SessionRepository::SetActiveLoginKey(const std::string& key) {
  if (key.empty()) return;
  database_.UpdateJson(
      kSessionKey,
      [&key](std::optional<nlohmann::json> current) -> std::optional<nlohmann::json> {
        nlohmann::json payload;
        if (current && current->is_object()) {
          payload = *current;
        } else {
          payload = {{"version", kProtectedSessionVersion}, {"gen", 0}};
        }
        payload[kActiveLoginKey] = key;
        return payload;
      });
}

std::string SessionRepository::BeginLoginAttempt(const std::string& attemptId) {
  const auto id = attemptId.empty() ? NextLazyVipClaimId(CurrentProcessEpoch()) : attemptId;
  database_.UpdateJson(kSessionKey, [&](std::optional<nlohmann::json> current) {
    auto payload = current && current->is_object() ? *current :
        nlohmann::json{{"version", kProtectedSessionVersion}, {"gen", 0}};
    payload.erase(kActiveLoginKey);
    payload["active_login_attempt"] = id;
    return std::optional<nlohmann::json>{std::move(payload)};
  });
  return id;
}

bool SessionRepository::PublishLoginKey(const std::string& attemptId, const std::string& key,
                                      const std::function<bool()>& canPublish,
                                      std::optional<model::IdentityVersion> expected) {
  if (attemptId.empty() || key.empty()) return false;
  return database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto current, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    if (!current || !current->is_object() ||
        current->value("active_login_attempt", "") != attemptId) return std::nullopt;
    if (expected && (ReadGeneration(*current) != expected->accountGeneration ||
        DeviceRevision(device) != expected->deviceRevision)) return std::nullopt;
    (*current)[kActiveLoginKey] = key;
    if (canPublish && !canPublish()) return std::nullopt;
    return std::pair{*current, device.value_or(nlohmann::json::object())};
  });
}

model::CommitResult SessionRepository::ReplaceDevice(model::IdentityVersion expected,
    const model::DeviceInfo& replacement, const execution::OperationContext& operation) {
  auto result = model::CommitResult::StaleAccount;
  database_.UpdateJsonPair(kSessionKey, "device.info", [&](auto session, auto device)
      -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
    const auto gen = session ? ReadGeneration(*session) : std::optional<std::uint64_t>{0};
    if (gen != expected.accountGeneration) return std::nullopt;
    result = model::CommitResult::StaleDevice;
    if (DeviceRevision(device) != expected.deviceRevision) return std::nullopt;
    auto payload = StampDevice(EncodeDevice(replacement), device);
    if (!operation.TryCommit()) {
      result = operation.Reason() == execution::StopReason::Deadline ? model::CommitResult::DeadlineBeforeCommit : model::CommitResult::CancelledBeforeCommit;
      return std::nullopt;
    }
    result = model::CommitResult::Applied;
    return std::pair{session.value_or(nlohmann::json::object()), std::move(payload)};
  });
  return result;
}

bool SessionRepository::IsLoginAttemptCurrent(std::uint64_t generation, const std::string& key) {
  const auto payload = database_.GetJson(kSessionKey);
  if (!payload || !payload->is_object() || key.empty()) return false;
  const auto gen = ReadGeneration(*payload);
  const auto field = payload->find(kActiveLoginKey);
  return gen && *gen == generation && field != payload->end() && field->is_string() && *field == key;
}

void SessionRepository::CancelLoginAttempt(const std::string& attemptId) {
  if (attemptId.empty()) return;
  database_.UpdateJson(kSessionKey, [&](std::optional<nlohmann::json> current) {
    if (!current || !current->is_object() ||
        current->value("active_login_attempt", "") != attemptId) return std::optional<nlohmann::json>{};
    current->erase(kActiveLoginKey);
    current->erase("active_login_attempt");
    return current;
  });
}

void SessionRepository::ClearActiveLoginKey(const std::string& key) {
  database_.UpdateJson(
      kSessionKey,
      [&key](std::optional<nlohmann::json> current) -> std::optional<nlohmann::json> {
        if (!current || !current->is_object()) return std::nullopt;
        if (!key.empty() && current->contains(kActiveLoginKey)) {
          if ((*current)[kActiveLoginKey] != key) return std::nullopt;
        }
        nlohmann::json payload = *current;
        payload.erase(kActiveLoginKey);
        return payload;
      });
}

bool SessionRepository::CommitLoginAttempt(
    std::uint64_t expectedGeneration,
    const std::string& key,
    const model::SessionInfo& session,
    const std::optional<model::DeviceInfo>& device,
    const std::function<bool()>& canCommit,
    std::optional<std::uint64_t> expectedDeviceRevision) {
  if (key.empty() || session.token.empty() || session.userId.empty()) return false;
  return database_.UpdateJsonPair(kSessionKey, "device.info",
      [&](std::optional<nlohmann::json> current, std::optional<nlohmann::json> storedDevice)
          -> std::optional<std::pair<nlohmann::json, nlohmann::json>> {
        if (expectedDeviceRevision && DeviceRevision(storedDevice) != *expectedDeviceRevision) return std::nullopt;
        if (!current || !current->is_object()) return std::nullopt;
        const auto generation = ReadGeneration(*current);
        const auto active = current->find(kActiveLoginKey);
        if (!generation || *generation != expectedGeneration ||
            active == current->end() || !active->is_string() || *active != key) return std::nullopt;
        const auto newGen = NextGeneration(current);
        auto plaintext = EncodeSession(session).dump();
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", newGen},
                                  {"protected_data", ProtectForCurrentUser(plaintext)}};
        SecureZeroMemory(plaintext.data(), plaintext.size());
        if (canCommit && !canCommit()) return std::nullopt;
        return std::pair{std::move(payload), device ? StampDevice(EncodeDevice(*device), storedDevice) :
            storedDevice.value_or(nlohmann::json::object())};
      });
}

std::optional<LazyVipRefreshTicket> SessionRepository::TryClaimLazyVipRefresh(
    std::uint64_t expectedGeneration,
    std::int64_t nowUnixMs,
    const std::string& processEpoch) {
  if (processEpoch.empty() || processEpoch.size() > 64) return std::nullopt;

  std::optional<LazyVipRefreshTicket> candidate;
  std::optional<nlohmann::json> committed;
  try {
    committed = database_.UpdateJson(
        kSessionKey,
        [&](std::optional<nlohmann::json> current)
            -> std::optional<nlohmann::json> {
          candidate.reset();
          if (!current || !current->is_object() || current->empty()) {
            return std::nullopt;
          }
          const auto generation = ReadGeneration(*current);
          if (!generation || *generation != expectedGeneration) {
            return std::nullopt;
          }
          const auto session = ReadProtectedSession(*current);
          if (!session || IsEmptySession(*session) || session->userId.empty() ||
              session->token.empty() || !session->vipToken.empty()) {
            return std::nullopt;
          }

          if (current->contains(kLazyVipRefreshKey) &&
              IsLazyVipRefreshSuppressed((*current)[kLazyVipRefreshKey],
                                         processEpoch, nowUnixMs)) {
            return std::nullopt;
          }

          LazyVipRefreshTicket ticket;
          ticket.generation = expectedGeneration;
          ticket.processEpoch = processEpoch;
          ticket.claimId = NextLazyVipClaimId(processEpoch);
          ticket.session = *session;

          nlohmann::json updated = *current;
          updated[kLazyVipRefreshKey] = {
              {"epoch", ticket.processEpoch},
              {"claim_id", ticket.claimId},
              {"in_flight", true},
          };
          candidate = std::move(ticket);
          return updated;
        });
  } catch (...) {
    return std::nullopt;
  }

  // UpdateJson returns a value only after its actor has committed the write.
  if (!committed || !candidate) return std::nullopt;
  return candidate;
}

bool SessionRepository::FinishLazyVipRefresh(
    const LazyVipRefreshTicket& ticket,
    std::int64_t completedUnixMs) noexcept {
  if (ticket.processEpoch.empty() || ticket.claimId.empty()) return false;

  bool applied = false;
  try {
    database_.UpdateJson(
        kSessionKey,
        [&](std::optional<nlohmann::json> current)
            -> std::optional<nlohmann::json> {
          applied = false;
          if (!current || !current->is_object() || current->empty()) {
            return std::nullopt;
          }
          const auto generation = ReadGeneration(*current);
          if (!generation || *generation != ticket.generation ||
              !current->contains(kLazyVipRefreshKey)) {
            return std::nullopt;
          }
          const auto& metadata = (*current)[kLazyVipRefreshKey];
          if (!metadata.is_object()) return std::nullopt;
          const auto inFlight = metadata.find("in_flight");
          const auto epoch = metadata.find("epoch");
          const auto claimId = metadata.find("claim_id");
          if (inFlight == metadata.end() || !inFlight->is_boolean() ||
              !inFlight->get<bool>() || epoch == metadata.end() ||
              !epoch->is_string() ||
              epoch->get<std::string>() != ticket.processEpoch ||
              claimId == metadata.end() || !claimId->is_string() ||
              claimId->get<std::string>() != ticket.claimId) {
            return std::nullopt;
          }

          nlohmann::json updated = *current;
          updated[kLazyVipRefreshKey] = {
              {"epoch", ticket.processEpoch},
              {"claim_id", ticket.claimId},
              {"in_flight", false},
              {"completed_at_ms", completedUnixMs},
              {"retry_after_ms", CooldownDeadline(completedUnixMs)},
          };
          applied = true;
          return updated;
        });
  } catch (...) {
    // Scope-guard cleanup must never replace a refresh exception or abort the
    // request merely because best-effort cooldown metadata could not persist.
    return false;
  }
  return applied;
}

std::string SessionRepository::CurrentProcessEpoch() {
  static const std::string epoch = [] {
    std::ostringstream value;
    try {
      std::random_device random;
      value << std::hex << std::setfill('0');
      for (int i = 0; i < 4; ++i) {
        value << std::setw(8) << static_cast<std::uint32_t>(random());
      }
    } catch (...) {
#ifdef _WIN32
      const auto ticks = static_cast<std::uint64_t>(GetTickCount64());
      const auto now = static_cast<std::uint64_t>(CurrentUnixTimeMs());
      const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
      const auto ticks = static_cast<std::uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count());
      const auto now = static_cast<std::uint64_t>(CurrentUnixTimeMs());
      const auto process = static_cast<std::uint64_t>(getpid());
#endif
      const auto first = ticks ^ (process << 32) ^ now;
      const auto second = now ^ (ticks << 1) ^ (process * 0x9e3779b97f4a7c15ULL);
      value << std::hex << std::setfill('0') << std::setw(16) << first
            << std::setw(16) << second;
    }
    return value.str();
  }();
  return epoch;
}

std::int64_t SessionRepository::CurrentUnixTimeMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(
             system_clock::now().time_since_epoch())
      .count();
}

}  // namespace echo::storage

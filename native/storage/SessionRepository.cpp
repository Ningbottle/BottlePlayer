#include "echo/storage/SessionRepository.h"

#include "echo/core/JsonHelpers.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <windows.h>
#include <wincrypt.h>

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

constexpr int kProtectedSessionVersion = 1;
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

bool IsEmptySession(const echo::core::SessionInfo& session) {
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

std::optional<echo::core::SessionInfo> ReadProtectedSession(
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
    return echo::core::SessionInfoFromJson(nlohmann::json::parse(*plaintext));
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
  auto payload = database_.GetJson(kSessionKey);
  if (!payload || !payload->is_object() || payload->empty()) return view;

  if (payload->value("version", 0) == kProtectedSessionVersion &&
      payload->contains("protected_data") && (*payload)["protected_data"].is_string()) {
    const auto plaintext =
        UnprotectForCurrentUser((*payload)["protected_data"].get<std::string>());
    if (!plaintext) return view;
    try {
      const auto session =
          echo::core::SessionInfoFromJson(nlohmann::json::parse(*plaintext));
      view.generation = payload->value("gen", 0);
      if (!IsEmptySession(session)) {
        view.session = session;
      }
      return view;
    } catch (const nlohmann::json::exception&) {
      return view;
    }
  }

  // One-time migration for databases created before session encryption.
  {
    const auto migratedFlag = database_.GetJson("session.encryption_migrated");
    const bool alreadyMigrated =
        migratedFlag && migratedFlag->is_boolean() && migratedFlag->get<bool>();
    if (alreadyMigrated) {
      // Migration already completed; a plaintext payload here is an anomaly
      // (bug, backup restore, or another writer). Do not trust it.
      ECHO_LOG("SessionRepository",
               "refusing plaintext session.info after migration; ignoring");
      return view;
    }
  }
  const auto session = echo::core::SessionInfoFromJson(*payload);
  if (IsEmptySession(session)) return view;
  // The migration write stamps the first real generation; a caller that
  // captured this snapshot must compare against THAT generation, not 0.
  view.generation = Save(session);
  view.session = session;
  database_.SetJson("session.encryption_migrated", true);
  return view;
}

std::optional<echo::core::SessionInfo> SessionRepository::Load() {
  return LoadSnapshot().session;
}

std::uint64_t SessionRepository::Save(const echo::core::SessionInfo& session) {
  // Identity-changing write: always wins, and always advances the account
  // generation so every in-flight request that captured the previous
  // generation becomes a rejected stale writer (B02).
  std::uint64_t generation = 0;
  database_.UpdateJson(
      kSessionKey,
      [&session, &generation](std::optional<nlohmann::json> current)
          -> std::optional<nlohmann::json> {
        const std::uint64_t currentGen =
            current && current->is_object() ? current->value("gen", 0) : 0;
        generation = currentGen + 1;
        auto plaintext = echo::core::ToJson(session).dump();
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", generation},
                                  {"protected_data", ProtectForCurrentUser(plaintext)}};
        SecureZeroMemory(plaintext.data(), plaintext.size());
        return payload;
      });
  return generation;}

void SessionRepository::Clear() {
  // Logout: keep the generation sequence advancing (an empty payload still
  // carries gen so late writers of the ended session are rejected), drop the
  // protected credentials.
  database_.UpdateJson(
      kSessionKey,
      [](std::optional<nlohmann::json> current) -> std::optional<nlohmann::json> {
        const std::uint64_t currentGen =
            current && current->is_object() ? current->value("gen", 0) : 0;
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", currentGen + 1}};
        return payload;
      });
}

bool SessionRepository::PatchIfGeneration(
    std::uint64_t expectedGeneration,
    const std::function<void(echo::core::SessionInfo&)>& mutator) {
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
        if (current->value("version", 0) != kProtectedSessionVersion ||
            !current->contains("protected_data") ||
            !(*current)["protected_data"].is_string()) {
          // Cannot read the stored session (legacy/anomalous payload) — never
          // overwrite it with a patch derived from a stale snapshot.
          return std::nullopt;
        }
        if (current->value("gen", 0) != expectedGeneration) {
          // The account moved on (logout / another account / re-login) while
          // this request was in flight. Reject the late writer.
          return std::nullopt;
        }
        const auto plaintext =
            UnprotectForCurrentUser((*current)["protected_data"].get<std::string>());
        if (!plaintext) return std::nullopt;
        echo::core::SessionInfo session;
        try {
          session = echo::core::SessionInfoFromJson(nlohmann::json::parse(*plaintext));
        } catch (const nlohmann::json::exception&) {
          return std::nullopt;
        }
        if (IsEmptySession(session)) return std::nullopt;

        mutator(session);
        applied = true;

        auto updated = echo::core::ToJson(session).dump();
        nlohmann::json payload = {{"version", kProtectedSessionVersion},
                                  {"gen", expectedGeneration},
                                  {"protected_data", ProtectForCurrentUser(updated)}};
        // Same-generation profile/credential patches must not erase the
        // out-of-process claim and cooldown metadata attached to session.info.
        if (current->contains(kLazyVipRefreshKey)) {
          payload[kLazyVipRefreshKey] = (*current)[kLazyVipRefreshKey];
        }
        SecureZeroMemory(updated.data(), updated.size());
        return payload;
      });
  return applied;
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
      const auto ticks = static_cast<std::uint64_t>(GetTickCount64());
      const auto now = static_cast<std::uint64_t>(CurrentUnixTimeMs());
      const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
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

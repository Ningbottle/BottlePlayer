#include "echo/storage/SessionRepository.h"

#include "echo/core/JsonHelpers.h"

#include <windows.h>
#include <wincrypt.h>

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
    LocalFree(output.pbData);
    return encoded;
  } catch (...) {
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
  LocalFree(output.pbData);
  return plaintext;
}

bool IsEmptySession(const echo::core::SessionInfo& session) {
  return session.token.empty() && session.userId.empty() && session.t1.empty() &&
         session.nickname.empty() && session.pic.empty();
}

}  // namespace

namespace echo::storage {

SessionRepository::SessionRepository(Database& database) : database_(database) {}

std::optional<echo::core::SessionInfo> SessionRepository::Load() {
  auto payload = database_.GetJson("session.info");
  if (!payload || !payload->is_object() || payload->empty()) return std::nullopt;

  if (payload->value("version", 0) == kProtectedSessionVersion &&
      payload->contains("protected_data") && (*payload)["protected_data"].is_string()) {
    const auto plaintext =
        UnprotectForCurrentUser((*payload)["protected_data"].get<std::string>());
    if (!plaintext) return std::nullopt;
    try {
      const auto session =
          echo::core::SessionInfoFromJson(nlohmann::json::parse(*plaintext));
      return IsEmptySession(session) ? std::nullopt
                                     : std::optional<echo::core::SessionInfo>(session);
    } catch (const nlohmann::json::exception&) {
      return std::nullopt;
    }
  }

  // One-time migration for databases created before session encryption.
  const auto session = echo::core::SessionInfoFromJson(*payload);
  if (IsEmptySession(session)) return std::nullopt;
  Save(session);
  return session;
}

void SessionRepository::Save(const echo::core::SessionInfo& session) {
  const auto plaintext = echo::core::ToJson(session).dump();
  database_.SetJson(
      "session.info",
      {{"version", kProtectedSessionVersion},
       {"protected_data", ProtectForCurrentUser(plaintext)}});
}

void SessionRepository::Clear() {
  database_.SetJson("session.info", nlohmann::json::object());
}

}  // namespace echo::storage



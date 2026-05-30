#include "echo/core/UserCloudService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"

#include <windows.h>
#include <wincrypt.h>

#include <ctime>
#include <sstream>
#include <iomanip>
#include <vector>

namespace echo::core {
namespace {


std::string Base64Encode(const std::vector<BYTE>& data) {
  DWORD b64Len = 0;
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64Len)) {
    return {};
  }
  std::string b64Str(b64Len, '\0');
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64Str[0], &b64Len)) {
    return {};
  }
  while (!b64Str.empty() && (b64Str.back() == '\0' || b64Str.back() == '\r' || b64Str.back() == '\n')) {
    b64Str.pop_back();
  }
  return b64Str;
}

std::vector<BYTE> Base64Decode(const std::string& b64Str) {
  DWORD decodedLen = 0;
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, nullptr, &decodedLen, nullptr, nullptr)) {
    return {};
  }
  std::vector<BYTE> decoded(decodedLen);
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, decoded.data(), &decodedLen, nullptr, nullptr)) {
    return {};
  }
  return decoded;
}

nlohmann::json MakeError(const std::string& message, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", message},
      {"status_code", statusCode},
  };
}

}  // namespace

UserCloudService::UserCloudService()
    : UserCloudService([](const std::string& url,
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Post(url, body, headers);
      }) {}

UserCloudService::UserCloudService(UserCloudHttpPost httpPost)
    : httpPost_(std::move(httpPost)) {}

nlohmann::json UserCloudService::GetList(
    const std::string& userId,
    const std::string& token,
    int page,
    int pageSize) const {
  const auto clienttime = std::to_string(std::time(nullptr));

  // 1. Prepare dataMap and AES encrypt it
  nlohmann::json dataMap = {
      {"page", page},
      {"pagesize", pageSize},
      {"getkmr", 1}
  };
  std::string plaintext = dataMap.dump();
  AesKeyPair aesKeyPair = PlaylistAesEncrypt(plaintext);
  if (aesKeyPair.key.empty() || aesKeyPair.data.empty()) {
    return MakeError("AES encryption failed");
  }

  // Decode Base64 ciphertext into raw binary payload for POST body
  std::vector<BYTE> binaryCipher = Base64Decode(aesKeyPair.data);
  std::string postBody(reinterpret_cast<char*>(binaryCipher.data()), binaryCipher.size());

  // 2. Prepare RSA encrypted p parameter
  nlohmann::json rsaPayload = {
      {"aes", aesKeyPair.key},
      {"uid", userId.empty() ? 0 : std::stoll(userId)},
      {"token", token}
  };
  std::string p = RsaPkcs1Encrypt(rsaPayload.dump());

  // 3. Construct parameters map
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  std::unordered_map<std::string, std::string> paramsMap;
  paramsMap["clienttime"] = clienttime;
  paramsMap["mid"] = "0"; // Default MID
  paramsMap["key"] = SignParamsKey(clienttime, profile.appid, profile.clientver, profile.saltKind);
  paramsMap["clientver"] = profile.clientver;
  paramsMap["appid"] = profile.appid;
  paramsMap["p"] = p;

  // Build sorted query string (clearDefaultParams: true, notSignature: true)
  std::vector<std::string> keys;
  keys.reserve(paramsMap.size());
  for (const auto& [k, _] : paramsMap) keys.push_back(k);
  std::sort(keys.begin(), keys.end());

  std::ostringstream urlStream;
  urlStream << "https://mcloudservice.kugou.com/v1/get_list?";
  bool first = true;
  for (const auto& key : keys) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(paramsMap[key]);
    first = false;
  }
  std::string url = urlStream.str();

  const auto result = httpPost_(
      url,
      postBody,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/octet-stream"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  // Convert response body back to base64 for AES decryption
  std::string base64Resp = Base64Encode(std::vector<BYTE>(result.body.begin(), result.body.end()));
  std::string decryptedBody = PlaylistAesDecrypt(base64Resp, aesKeyPair.key);

  if (decryptedBody.empty()) {
    return MakeError("AES decryption of response failed");
  }

  try {
    return nlohmann::json::parse(decryptedBody);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error of decrypted response: ") + e.what());
  }
}

}  // namespace echo::core

#include "echo/core/LoginService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/StringUtils.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/rand.h>
#include "echo/core/PortableBase64.h"
#endif
#include <array>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace echo::core {
namespace {

std::string SecureTemporaryKey() {
  static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string key;
  key.reserve(16);
  std::array<unsigned char, 32> random{};
  while (key.size() < 16) {
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
      SecureZeroMemory(random.data(), random.size());
      return {};
    }
#else
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) {
      portable::SecureZero(random.data(), random.size());
      return {};
    }
#endif
    for (const auto byte : random) {
      if (byte >= 252) continue; // rejection sampling avoids modulo bias
      key += alphabet[byte % 36];
      if (key.size() == 16) break;
    }
  }
#ifdef _WIN32
  SecureZeroMemory(random.data(), random.size());
#else
  portable::SecureZero(random.data(), random.size());
#endif
  return key;
}

std::string BuildSignedUrl(
    const std::string& baseUrl,
    const std::unordered_map<std::string, std::string>& params) {
  const std::string signature = SignatureWebParams(params);
  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }
  urlStream << "&signature=" << signature;
  return urlStream.str();
}

nlohmann::json MakeErrorJson(const std::string& errorMsg, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", errorMsg},
      {"status_code", statusCode}
  };
}

std::string JsonString(const nlohmann::json& value, const char* key) {
  if (!value.contains(key)) return {};
  const auto& field = value.at(key);
  if (field.is_string()) return field.get<std::string>();
  if (field.is_number_integer()) return std::to_string(field.get<std::int64_t>());
  if (field.is_number_unsigned()) return std::to_string(field.get<std::uint64_t>());
  return {};
}

int JsonInt(const nlohmann::json& value, const char* key, int fallback = 0) {
  if (!value.contains(key)) return fallback;
  const auto& field = value.at(key);
  if (field.is_number_integer()) return field.get<int>();
  if (field.is_number_unsigned()) return static_cast<int>(field.get<unsigned int>());
  if (field.is_string()) {
    try {
      return std::stoi(field.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

std::string DescribeJsonShape(const nlohmann::json& value) {
  if (!value.is_object()) return value.type_name();
  std::ostringstream out;
  bool first = true;
  for (const auto& item : value.items()) {
    if (!first) out << ',';
    first = false;
    out << item.key() << ':' << item.value().type_name();
    if (item.value().is_string()) {
      out << "(len=" << item.value().get_ref<const std::string&>().size() << ')';
    }
  }
  return out.str();
}

LoginHttpGetWithRetry RetryAware(LoginHttpGet handler) {
  if (!handler) return {};
  return [handler = std::move(handler)](
      const std::string& url,
      const std::unordered_map<std::string, std::string>& headers,
      bool) {
    return handler(url, headers);
  };
}

}  // namespace

LoginService::LoginService()
    : LoginService(
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers,
             bool retryAllowed) {
            HttpClient client;
            // QR channel creation has a remote side effect despite using GET.
            // The page owns refresh/polling; transport must not replay it.
            return client.Get(url, headers, kHttpGetDefaultBudgetMs, 10 * 1024 * 1024, nullptr, retryAllowed);
          },
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers,
             bool retryAllowed) {
            HttpClient client;
            // Polling is the read-only half of the same route and is safe to
            // replay: without a retry a single blip fails a poll the page shows
            // to the user.
            return client.Get(url, headers, kHttpGetDefaultBudgetMs, 10 * 1024 * 1024, nullptr, retryAllowed);
          },
          [](const std::string& url,
             const std::string& body,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          }) {}

LoginService::LoginService(LoginHttpGet httpGet)
    : LoginService(httpGet, httpGet, [](const std::string& url,
                                        const std::string& body,
                                        const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          }) {}

LoginService::LoginService(LoginHttpGet httpGet, LoginHttpPost httpPost)
    : LoginService(httpGet, httpGet, std::move(httpPost)) {}

LoginService::LoginService(LoginHttpGet httpGet, LoginHttpGet httpGetPoll, LoginHttpPost httpPost)
    : LoginService(RetryAware(std::move(httpGet)), RetryAware(std::move(httpGetPoll)),
                   std::move(httpPost)) {}

LoginService::LoginService(
    LoginHttpGetWithRetry httpGet,
    LoginHttpGetWithRetry httpGetPoll,
    LoginHttpPost httpPost)
    : httpGet_(std::move(httpGet)), httpGetPoll_(std::move(httpGetPoll)), httpPost_(std::move(httpPost)) {}

nlohmann::json LoginService::BeginQrLogin(const DeviceInfo& device) const {
  if (!httpGet_) {
    // Same guard as PollQrLogin: an unwired GET must fail the call, not invoke
    // an empty std::function.
    return MakeErrorJson("No HTTP GET handler available", 0);
  }
  const auto profile = GetKuGouProfile(kProjectEdition);
  // For /v2/qrcode, KuGou expects appid=1001 or 1014 in the GET parameters,
  // while qrcode_txt carries the target Android edition. Keep generation and
  // polling on the project edition so the resulting token can mint vip_token.
  std::unordered_map<std::string, std::string> params = {
      {"appid", QrLoginAppId},
      {"clientver", profile.clientver},
      {"type", "1"},
      {"plat", "4"},
      {"qrcode_txt", "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=" +
                         profile.appid + "&"},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", ResolveAndroidMid(device)},
      {"uuid", "-"},
      {"dfid", device.dfid}
  };

  const std::string url = BuildSignedUrl("https://login-user.kugou.com/v2/qrcode", params);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
      }, false);

  if (!result.error.empty()) {
    // A field report for "the QR never appeared" is only actionable if the
    // user's own log says WHICH link failed. Classify here: DNS
    // (name_not_resolved), TCP/TLS (winhttp error code), or the deadline /
    // cancellation that fires when the scheduler budget expires first. Never
    // log the signed URL — it carries the device fingerprint.
    ECHO_LOG("QrLogin", std::string("qr_key_transport_failed error=") +
                           result.error + " http_status=" +
                           std::to_string(result.statusCode) + " winhttp=" +
                           std::to_string(result.winHttpErrorCode) + " attempts=" +
                           std::to_string(result.attemptCount) + " elapsed_ms=" +
                           std::to_string(result.elapsedMs));
    return MakeErrorJson(result.error, result.statusCode);
  }

  try {
    auto j = nlohmann::json::parse(result.body);
    // JsonInt, not j.value(): upstream sometimes sends status as a string, and
    // a non-object body must not turn a diagnostic into a parse exception.
    const int upstreamStatus = j.is_object() ? JsonInt(j, "status") : -1;
    if (upstreamStatus != 1) {
      // Upstream answered but refused (signature rejected, throttled, key
      // shape changed). This is a DIFFERENT incident from a dead link, and the
      // field name that moved is the only way to tell — record the shape, not
      // the values.
      ECHO_LOG("QrLogin", std::string("qr_key_rejected status=") +
                             std::to_string(upstreamStatus) + " error_code=" +
                             JsonString(j, "error_code") + " body_len=" +
                             std::to_string(result.body.size()) + " shape={" +
                             DescribeJsonShape(j) + "}");
    }
    // 移除 debug_url，因为它包含设备指纹和签名，会泄露安全信息
    // j["debug_url"] = url;
    
    // 添加脱敏的调试信息（不含敏感参数）
    #ifdef _DEBUG
    nlohmann::json debug_info = {
      {"endpoint", "qrcode"},
      {"status", j.value("status", 0)}
    };
    j["_debug"] = debug_info;
    #endif
    
    return j;
  } catch (const nlohmann::json::exception& e) {
    // A non-JSON body on a 200 is the signature of a captive portal / ISP
    // interception page — record the prefix length so it can be told apart
    // from a truncated body.
    ECHO_LOG("QrLogin", std::string("qr_key_body_not_json http_status=") +
                           std::to_string(result.statusCode) + " body_len=" +
                           std::to_string(result.body.size()));
    return MakeErrorJson("upstream_invalid_json", result.statusCode);
  }
}

nlohmann::json LoginService::PollQrLogin(const DeviceInfo& device, const std::string& key) const {
  if (!httpGetPoll_) {
    // Never invoke an empty std::function: it throws std::bad_function_call and
    // terminates the process instead of failing the call. Polling has its own
    // seam so an empty polling handler must fail before invocation.
    return MakeErrorJson("No HTTP GET handler available", 0);
  }
  const auto profile = GetKuGouProfile(kProjectEdition);
  std::unordered_map<std::string, std::string> params = {
      {"plat", "4"},
      {"appid", profile.appid},
      {"clientver", profile.clientver},
      {"qrcode", key},
      {"srcappid", "2919"},
      {"clienttime", std::to_string(std::time(nullptr))},
      {"mid", ResolveAndroidMid(device)},
      {"uuid", "-"},
      {"dfid", device.dfid}
  };
  if (!device.serverDev.empty()) params["dev"] = device.serverDev;

  const std::string url = BuildSignedUrl("https://login-user.kugou.com/v2/get_userinfo_qrcode", params);

  const auto result = httpGetPoll_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"},
      }, true);

  if (!result.error.empty()) {
    ECHO_LOG("QrLogin", std::string("qr_poll_transport_failed error=") +
                           result.error + " http_status=" +
                           std::to_string(result.statusCode) + " winhttp=" +
                           std::to_string(result.winHttpErrorCode) + " attempts=" +
                           std::to_string(result.attemptCount) + " elapsed_ms=" +
                           std::to_string(result.elapsedMs));
    return MakeErrorJson(result.error, result.statusCode);
  }

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeErrorJson("upstream_invalid_json", result.statusCode);
  }
}

std::optional<LoginRefreshResult> LoginService::RefreshSession(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    const std::string& t1) const {
  if (!httpPost_) {
    // Never invoke an empty std::function: it throws std::bad_function_call and
    // terminates the process. A host that wired only the GET verb gets a plain
    // "no refresh" answer instead of a crash or a real network call.
    return std::nullopt;
  }
  auto refreshOnce = [&](KuGouEdition edition,
                         const std::string& currentToken,
                         const std::string& currentT1)
      -> std::optional<LoginRefreshResult> {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
    const long long sec = ms / 1000;
    const bool lite = edition == KuGouEdition::Concept;
    const char* editionName = lite ? "concept" : "standard";
    const auto profile = GetKuGouProfile(edition);

    // cryptoAesEncrypt with an explicit key returns lowercase HEX, not Base64.
    const std::string p3 = AesCbcEncryptHex(
        "{\"clienttime\":" + std::to_string(sec) + ",\"token\":\"" +
            currentToken + "\"}",
        lite ? "c24f74ca2820225badc01946dba4fdf7"
             : "90b8382a1bb4ccdcf063102053fd75b8",
        lite ? "adc01946dba4fdf7" : "f063102053fd75b8");

    // encryptParams = AES({}, md5(tempKey)[0:32], 后16位)；tempKey 同时进 RSA 包装。
    const std::string tempKey = SecureTemporaryKey();
    if (tempKey.empty()) {
      ECHO_LOG("VipToken", "login_by_token_secure_random_failed");
      return std::nullopt;
    }
    const std::string md5 = CalculateMd5(tempKey);
    const std::string aesKey = md5.substr(0, 32);
    const std::string aesIv = aesKey.substr(16, 16);
    const std::string paramsStr = AesCbcEncryptHex("{}", aesKey, aesIv);
    const std::string pk = RsaRawEncryptRef(
        "{\"clienttime_ms\":" + std::to_string(ms) + ",\"key\":\"" +
            tempKey + "\"}",
        profile.saltKind);
    if (p3.empty() || paramsStr.empty() || pk.empty()) {
      ECHO_LOG("VipToken", std::string("login_by_token crypto prep failed edition=") +
          editionName);
      return std::nullopt;
    }

    nlohmann::json bodyJson = {
        {"dfid", device.dfid.empty() ? "-" : device.dfid},
        {"p3", p3},
        {"plat", 1},
        {"t1", 0},
        {"t2", 0},
        {"t3", "MCwwLDAsMCwwLDAsMCwwLDA="},
        {"pk", pk},
        {"params", paramsStr},
        {"userid", [userId] {
          try { return std::stoi(userId); } catch (...) { return 0; }
        }()},
        {"clienttime_ms", ms},
    };
    if (lite) {
      bodyJson["t1"] = AesCbcEncryptHex(
          currentT1 + "|" + std::to_string(ms),
          "5e4ef500e9597fe004bd09a46d8add98", "04bd09a46d8add98");
      bodyJson["t2"] = AesCbcEncryptHex(
          device.guid + "|0f607264fc6318a92b9e13c65db7cd3c|" + device.mac +
              "|" + device.serverDev + "|" + std::to_string(ms),
          "fd14b35e3f81af3817a20ae7adae7020", "17a20ae7adae7020");
      bodyJson["dev"] = device.serverDev;
    }
    const std::string body = bodyJson.dump();

    KuGouAndroidRequest req;
    // The HTTPS gateway uses a valid single-level hostname. Other login
    // operations use the same login router; do not downgrade credentials to
    // HTTP or bypass the direct host's certificate name mismatch.
    req.endpoint = "https://gateway.kugou.com/v5/login_by_token";
    req.profile = profile;
    req.device = device;
    req.body = body;
    req.params["clienttime"] = std::to_string(sec);
    if (!userId.empty()) req.params["userid"] = userId;
    if (!currentToken.empty()) req.params["token"] = currentToken;
    const std::string url = BuildSignedUrl(req);

    if (url.rfind("http://", 0) == 0) {
      ECHO_LOG("VipToken", "insecure_http_transport_refused: credentials cannot be transmitted over HTTP");
      return std::nullopt;
    }

    auto headers = BuildAndroidHeaders(req);
    headers["x-router"] = "login.user.kugou.com";
    headers["Content-Type"] = "application/json";
    headers["User-Agent"] =
        "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";
    const auto result = httpPost_(url, body, std::move(headers));
    if (!result.error.empty() || result.statusCode < 200 || result.statusCode >= 300) {
      ECHO_LOG("VipToken", std::string("login_by_token http failed edition=") +
          editionName + " error=" + result.error);
      return std::nullopt;
    }
    try {
      auto json = nlohmann::json::parse(result.body);
      if (json.value("status", 0) != 1 || !json.contains("data") ||
          !json["data"].is_object()) {
        ECHO_LOG("VipToken", std::string("login_by_token status!=1 edition=") +
            editionName + " body_len=" + std::to_string(result.body.size()));
        return std::nullopt;
      }
      auto data = json["data"];
      if (data.contains("secu_params") && data["secu_params"].is_string()) {
        const auto secuParams = data["secu_params"].get<std::string>();
        const auto decrypted = AesCbcDecryptHex(secuParams, aesKey, aesIv);
        auto inner = nlohmann::json::parse(decrypted, nullptr, false);
        ECHO_LOG("VipToken", std::string("login_by_token secu_params edition=") +
            editionName + " len=" + std::to_string(secuParams.size()) +
            " decrypted_len=" + std::to_string(decrypted.size()) +
            " inner_shape=" + DescribeJsonShape(inner));
        if (!inner.is_discarded() && inner.is_object()) {
          for (const auto& item : inner.items()) {
            data[item.key()] = item.value();
          }
        }
      }

      LoginRefreshResult refreshed;
      refreshed.token = JsonString(data, "token");
      if (refreshed.token.empty()) refreshed.token = currentToken;
      refreshed.vipToken = JsonString(data, "vip_token");
      refreshed.vipType = JsonInt(data, "vip_type");
      refreshed.t1 = JsonString(data, "t1");
      if (refreshed.t1.empty()) refreshed.t1 = currentT1;
      // Success is logged either way, with presence and lengths only, never
      // values. The success branch used to be silent, which is why a credential
      // that really was minted left no trace and could be mistaken for a
      // failure — the reason the signature-family bug survived two months.
      if (refreshed.vipToken.empty()) {
        ECHO_LOG("VipToken", std::string("login_by_token ok but no vip_token edition=") +
            editionName + " vip_token_key=" + (data.contains("vip_token") ? "Y" : "N") +
            " t1_len=" + std::to_string(refreshed.t1.size()) +
            " data_shape=" + DescribeJsonShape(data));
      } else {
        ECHO_LOG("VipToken", std::string("login_by_token ok edition=") + editionName +
            " vip_token_key=Y vip_token_len=" + std::to_string(refreshed.vipToken.size()) +
            " t1_len=" + std::to_string(refreshed.t1.size()));
      }
      return refreshed;
    } catch (const nlohmann::json::exception&) {
      ECHO_LOG("VipToken", std::string("login_by_token bad json edition=") +
          editionName);
    }
    return std::nullopt;
  };

  // One refresh, on the family that minted the session. The Lite contract signs
  // with a real registered dfid, so without one there is nothing to attempt.
  // The previous code fell back to the Standard family instead, which now
  // answers login_by_token with 20018, and then kept whatever ordinary token
  // that failing call happened to rotate. A credential minted by a family whose
  // claims upstream rejects is not a partial success — it is precisely the
  // state that looked like one and let the mismatch hide for two months.
  // See ADR-0005 追记.
  if (!device.registered || device.dfid.empty() || device.dfid == "-") {
    ECHO_LOG("VipToken", "refresh_skipped_unregistered_device");
    return std::nullopt;
  }

  return refreshOnce(kProjectEdition, token, t1);
}

std::string LoginService::RefreshVipToken(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token) const {
  const auto refreshed = RefreshSession(device, userId, token);
  return refreshed ? refreshed->vipToken : std::string{};
}

}  // namespace echo::core

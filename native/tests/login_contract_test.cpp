#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "echo/core/Crypto.h"
#include "echo/core/JsonHelpers.h"
#include "echo/core/LoginService.h"

namespace {

std::string QueryValue(const std::string& url, const std::string& key) {
  const auto marker = key + "=";
  const auto start = url.find(marker);
  if (start == std::string::npos) return {};
  const auto valueStart = start + marker.size();
  const auto end = url.find('&', valueStart);
  return url.substr(valueStart, end == std::string::npos ? std::string::npos
                                                         : end - valueStart);
}

}  // namespace

int main() {
  std::vector<std::string> urls;
  echo::core::LoginService service(
      [&](const std::string& url,
          const std::unordered_map<std::string, std::string>&) {
        urls.push_back(url);
        return echo::core::HttpResult{200, R"({"status":1})", ""};
      });

  echo::core::DeviceInfo device;
  device.dfid = "abcdefghijklmnopqrstuvwx";
  device.mid = "123456789012345678901234567890123456789";
  device.clientver = "11440";
  device.serverDev = "lite-device-id";

  service.BeginQrLogin(device);
  service.PollQrLogin(device, "qr-key");

  if (urls.size() != 2) {
    std::cerr << "[LoginContract] expected two requests" << std::endl;
    return 1;
  }

  const bool beginMatchesReference =
      urls[0].find("https://login-user.kugou.com/v2/qrcode?") == 0 &&
      QueryValue(urls[0], "appid") == "1001" &&
      QueryValue(urls[0], "clientver") == "11440" &&
      QueryValue(urls[0], "qrcode_txt").find("appid%3D3116") != std::string::npos;
  const bool pollMatchesReference =
      urls[1].find("https://login-user.kugou.com/v2/get_userinfo_qrcode?") == 0 &&
      QueryValue(urls[1], "appid") == "3116" &&
      QueryValue(urls[1], "clientver") == "11440" &&
      QueryValue(urls[1], "dev") == device.serverDev;

  if (!beginMatchesReference || !pollMatchesReference) {
    std::cerr << "[LoginContract] QR login must mint a concept Android token"
              << std::endl;
    return 1;
  }

  std::cout << "[LoginContract] Testing login_by_token refresh contract..." << std::endl;
  std::string refreshUrl;
  std::string refreshBody;
  std::unordered_map<std::string, std::string> refreshHeaders;
  echo::core::LoginService refreshService(
      [](const std::string&,
         const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "{}", "unexpected GET"};
      },
      [&](const std::string& url,
          const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        refreshUrl = url;
        refreshBody = body;
        refreshHeaders = headers;
        return echo::core::HttpResult{
            200,
            R"({"status":1,"data":{"token":"rotated-token","vip_token":"vip-token","vip_type":3,"t1":"new-t1"}})",
            ""};
      });

  const auto refreshed = refreshService.RefreshSession(device, "42", "old-token");
  if (!refreshed || refreshed->token != "rotated-token" ||
      refreshed->vipToken != "vip-token" || refreshed->vipType != 3 ||
      refreshed->t1 != "new-t1") {
    std::cerr << "[LoginContract] login_by_token response fields were not preserved"
              << std::endl;
    return 1;
  }

  const auto wireBody = nlohmann::json::parse(refreshBody);
  const auto isLowerHex = [](const std::string& value) {
    return !value.empty() && value.size() % 32 == 0 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
             return std::isdigit(c) || (c >= 'a' && c <= 'f');
           });
  };
  const auto clienttime = QueryValue(refreshUrl, "clienttime");
  const std::unordered_map<std::string, std::string> signedParams = {
      {"appid", QueryValue(refreshUrl, "appid")},
      {"clientver", QueryValue(refreshUrl, "clientver")},
      {"clienttime", clienttime},
      {"dfid", QueryValue(refreshUrl, "dfid")},
      {"mid", QueryValue(refreshUrl, "mid")},
      {"uuid", QueryValue(refreshUrl, "uuid")},
      {"userid", QueryValue(refreshUrl, "userid")},
      {"token", QueryValue(refreshUrl, "token")},
  };
  const bool refreshMatchesReference =
      refreshUrl.find("http://login.user.kugou.com/v5/login_by_token?") == 0 &&
      QueryValue(refreshUrl, "appid") == "1005" &&
      QueryValue(refreshUrl, "clientver") == "20489" &&
      QueryValue(refreshUrl, "dfid") == device.dfid &&
      QueryValue(refreshUrl, "mid") == device.mid &&
      QueryValue(refreshUrl, "uuid") == "-" &&
      QueryValue(refreshUrl, "userid") == "42" &&
      QueryValue(refreshUrl, "token") == "old-token" &&
      QueryValue(refreshUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              signedParams, refreshBody, echo::core::KuGouSaltKind::Standard) &&
      wireBody.value("dfid", std::string{}) == device.dfid &&
      wireBody.value("userid", 0) == 42 &&
      isLowerHex(wireBody.value("p3", std::string{})) &&
      isLowerHex(wireBody.value("params", std::string{})) &&
      refreshHeaders["clienttime"] == clienttime &&
      refreshHeaders["dfid"] == device.dfid &&
      refreshHeaders["mid"] == device.mid &&
      refreshHeaders["kg-rc"] == "1" &&
      refreshHeaders["kg-thash"] == "5d816a0" &&
      refreshHeaders["kg-rec"] == "1" &&
      refreshHeaders["kg-rf"] == "B9EDA08A64250DEFFBCADDEE00F8F25F";
  if (!refreshMatchesReference) {
    std::cerr << "[LoginContract] login_by_token request does not match the reference contract"
              << std::endl;
    return 1;
  }

  std::cout << "[LoginContract] Testing concept VIP fallback contract..." << std::endl;
  auto conceptDevice = device;
  conceptDevice.registered = true;
  conceptDevice.guid = "guid-for-lite-refresh";
  conceptDevice.mac = "02:00:00:00:00:00";
  conceptDevice.serverDev = "lite-device-id";
  std::vector<std::string> fallbackUrls;
  std::vector<std::string> fallbackBodies;
  std::vector<std::unordered_map<std::string, std::string>> fallbackHeaders;
  echo::core::LoginService fallbackService(
      [](const std::string&,
         const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "{}", "unexpected GET"};
      },
      [&](const std::string& url,
          const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        fallbackUrls.push_back(url);
        fallbackBodies.push_back(body);
        fallbackHeaders.push_back(headers);
        if (fallbackUrls.size() == 1) {
          return echo::core::HttpResult{
              200,
              R"({"status":1,"data":{"token":"standard-rotated","vip_token":"","vip_type":0,"t1":"standard-t1"}})",
              ""};
        }
        return echo::core::HttpResult{
            200,
            R"({"status":1,"data":{"token":"concept-rotated","vip_token":"concept-vip-token","vip_type":3,"t1":"concept-t1"}})",
            ""};
      });

  const auto conceptRefreshed = fallbackService.RefreshSession(
      conceptDevice, "42", "old-token", "old-t1");
  if (!conceptRefreshed || fallbackUrls.size() != 2 ||
      conceptRefreshed->token != "concept-rotated" ||
      conceptRefreshed->vipToken != "concept-vip-token" ||
      conceptRefreshed->vipType != 3 || conceptRefreshed->t1 != "concept-t1") {
    std::cerr << "[LoginContract] concept fallback did not preserve the VIP session"
              << std::endl;
    return 1;
  }

  const auto conceptBody = nlohmann::json::parse(fallbackBodies[1]);
  const auto conceptClienttime = QueryValue(fallbackUrls[1], "clienttime");
  const std::unordered_map<std::string, std::string> conceptSignedParams = {
      {"appid", QueryValue(fallbackUrls[1], "appid")},
      {"clientver", QueryValue(fallbackUrls[1], "clientver")},
      {"clienttime", conceptClienttime},
      {"dfid", QueryValue(fallbackUrls[1], "dfid")},
      {"mid", QueryValue(fallbackUrls[1], "mid")},
      {"uuid", QueryValue(fallbackUrls[1], "uuid")},
      {"userid", QueryValue(fallbackUrls[1], "userid")},
      {"token", QueryValue(fallbackUrls[1], "token")},
  };
  const auto conceptP3 = nlohmann::json::parse(echo::core::AesCbcDecryptHex(
      conceptBody.value("p3", std::string{}),
      "c24f74ca2820225badc01946dba4fdf7", "adc01946dba4fdf7"));
  const auto conceptT1 = echo::core::AesCbcDecryptHex(
      conceptBody.value("t1", std::string{}),
      "5e4ef500e9597fe004bd09a46d8add98", "04bd09a46d8add98");
  const auto conceptT2 = echo::core::AesCbcDecryptHex(
      conceptBody.value("t2", std::string{}),
      "fd14b35e3f81af3817a20ae7adae7020", "17a20ae7adae7020");
  const auto conceptMs = std::to_string(conceptBody.value("clienttime_ms", 0LL));
  const bool conceptMatchesReference =
      QueryValue(fallbackUrls[1], "appid") == "3116" &&
      QueryValue(fallbackUrls[1], "clientver") == "11440" &&
      QueryValue(fallbackUrls[1], "dfid") == conceptDevice.dfid &&
      QueryValue(fallbackUrls[1], "token") == "standard-rotated" &&
      QueryValue(fallbackUrls[1], "signature") ==
          echo::core::SignatureAndroidParams(
              conceptSignedParams, fallbackBodies[1], echo::core::KuGouSaltKind::Lite) &&
      conceptBody.value("dev", std::string{}) == conceptDevice.serverDev &&
      conceptP3.value("token", std::string{}) == "standard-rotated" &&
      conceptT1 == "standard-t1|" + conceptMs &&
      conceptT2 == conceptDevice.guid +
          "|0f607264fc6318a92b9e13c65db7cd3c|" + conceptDevice.mac + "|" +
          conceptDevice.serverDev + "|" + conceptMs &&
      fallbackHeaders[1].at("dfid") == conceptDevice.dfid;
  if (!conceptMatchesReference) {
    std::cerr << "[LoginContract] concept fallback does not match the lite contract"
              << std::endl;
    return 1;
  }

  int conceptOnlyCalls = 0;
  echo::core::LoginService conceptOnlyService(
      [](const std::string&,
         const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "{}", "unexpected GET"};
      },
      [&](const std::string&,
          const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        ++conceptOnlyCalls;
        if (conceptOnlyCalls == 1) {
          return echo::core::HttpResult{
              200, R"({"status":0,"data":null,"error_code":20018})", ""};
        }
        return echo::core::HttpResult{
            200,
            R"({"status":1,"data":{"token":"concept-token","vip_token":"concept-vip","vip_type":3,"t1":"concept-t1"}})",
            ""};
      });
  const auto conceptOnly = conceptOnlyService.RefreshSession(
      conceptDevice, "42", "concept-login-token", "concept-login-t1");
  if (!conceptOnly || conceptOnlyCalls != 2 ||
      conceptOnly->vipToken != "concept-vip") {
    std::cerr << "[LoginContract] concept token was not retried after standard rejection"
              << std::endl;
    return 1;
  }

  const std::string aesKey = "0123456789abcdef0123456789abcdef";
  const std::string aesIv = "0123456789abcdef";
  const auto cipher = echo::core::AesCbcEncryptHex("{}", aesKey, aesIv);
  if (cipher != "05d4edd850aad74c4c2a603a3ac31367" ||
      echo::core::AesCbcDecryptHex(cipher, aesKey, aesIv) != "{}") {
    std::cerr << "[LoginContract] login_by_token AES wire encoding mismatch"
              << std::endl;
    return 1;
  }

  echo::core::SessionInfo stored;
  stored.token = refreshed->token;
  stored.userId = "42";
  stored.vipToken = refreshed->vipToken;
  stored.vipType = refreshed->vipType;
  stored.t1 = refreshed->t1;
  const auto restored = echo::core::SessionInfoFromJson(echo::core::ToJson(stored));
  if (restored.token != "rotated-token" || restored.vipToken != "vip-token" ||
      restored.vipType != 3 || restored.t1 != "new-t1") {
    std::cerr << "[LoginContract] refreshed VIP session fields were not persisted"
              << std::endl;
    return 1;
  }

  // Stage 7b: lack of VIP entitlement does not invalidate a successful token
  // rotation. Persist the latest successful edition as a whole session.
  for (const bool standardSucceeds : {false, true}) {
    int calls = 0;
    echo::core::LoginService rotationService({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      if (calls == 1) return echo::core::HttpResult{200, standardSucceeds
        ? R"({"status":1,"data":{"token":"standard-new","vip_token":"","vip_type":0,"t1":"standard-t1"}})"
        : R"({"status":0,"error_code":20018})", ""};
      return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"concept-new","vip_token":"","vip_type":0,"t1":"concept-new-t1"}})", ""};
    });
    const auto rotated = rotationService.RefreshSession(conceptDevice, "42", "old-token", "old-t1");
    if (!rotated || calls != 2 || rotated->token != "concept-new" ||
        rotated->t1 != "concept-new-t1" || !rotated->vipToken.empty() || rotated->vipType != 0) {
      std::cerr << "[LoginContract] successful concept rotation without VIP was discarded" << std::endl;
      return 1;
    }
  }

  // B1: the full dual-family ordering, each branch identified by the returned
  // credential bundle. A successful rotation must never be reverted to an
  // older token, and a rejected rotation must never invent entitlement.
  {
    // (a) Standard already issued a VIP token: the Concept family is not tried.
    int calls = 0;
    echo::core::LoginService standardVip({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"std-token","vip_token":"std-vip","vip_type":2,"t1":"std-t1"}})", ""};
    });
    const auto result = standardVip.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (!result || calls != 1 || result->token != "std-token" ||
        result->vipToken != "std-vip" || result->vipType != 2 || result->t1 != "std-t1") {
      std::cerr << "[LoginContract] standard VIP refresh did not stop after the first family" << std::endl;
      return 1;
    }
  }
  {
    // (b) Standard rotated the token but the Concept retry failed: keep the
    // last successful credential instead of discarding it.
    int calls = 0;
    echo::core::LoginService conceptFails({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      if (calls == 1) return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"std-kept","vip_token":"","vip_type":0,"t1":"std-t1"}})", ""};
      return echo::core::HttpResult{200, R"({"status":0,"error_code":20018})", ""};
    });
    const auto result = conceptFails.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (!result || calls != 2 || result->token != "std-kept" || result->t1 != "std-t1" ||
        !result->vipToken.empty() || result->vipType != 0) {
      std::cerr << "[LoginContract] failed concept retry discarded the last successful rotation" << std::endl;
      return 1;
    }
  }
  {
    // (c) Both families rejected: no credential is produced, so the caller
    // keeps the existing session untouched.
    int calls = 0;
    echo::core::LoginService bothFail({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200, R"({"status":0,"error_code":20018})", ""};
    });
    const auto result = bothFail.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (result || calls != 2) {
      std::cerr << "[LoginContract] rejected dual-family refresh produced a credential" << std::endl;
      return 1;
    }
  }

  std::cout << "[LoginContract] passed" << std::endl;
  return 0;
}

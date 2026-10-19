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
  auto start = url.find(marker);
  while (start != std::string::npos &&
         start != 0 && url[start - 1] != '?' && url[start - 1] != '&') {
    start = url.find(marker, start + 1);
  }
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
  device.registered = true;
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

  {
    echo::core::LoginService noPollHandler(
        [](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, R"({"status":1})", ""};
        },
        echo::core::LoginHttpGet{},
        [](const std::string&, const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{200, R"({"status":1})", ""};
        });
    const auto poll = noPollHandler.PollQrLogin(device, "qr-key");
    if (poll.value("status", 1) != 0 ||
        poll.value("error", std::string{}) != "No HTTP GET handler available") {
      std::cerr << "[LoginContract] an empty QR poll seam must return a structured error"
                << std::endl;
      return 1;
    }
  }

  std::cout << "[LoginContract] Testing login_by_token refresh contract..." << std::endl;
  std::string refreshUrl;
  std::string refreshBody;
  std::unordered_map<std::string, std::string> refreshHeaders;
  int refreshCalls = 0;
  echo::core::LoginService refreshService(
      [](const std::string&,
         const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "{}", "unexpected GET"};
      },
      [&](const std::string& url,
          const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        ++refreshCalls;
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
  // Exactly ONE refresh, signed with the family that minted the session. The
  // old contract was "Standard first, Concept as a fallback": every Standard
  // login_by_token now returns 20018, so that first call is a wasted
  // round-trip that also rotates the session token before the call that
  // actually works. See ADR-0005 追记.
  const bool refreshMatchesReference =
      refreshCalls == 1 &&
      refreshUrl.find("https://gateway.kugou.com/v5/login_by_token?") == 0 &&
      QueryValue(refreshUrl, "appid") == "3116" &&
      QueryValue(refreshUrl, "clientver") == "11440" &&
      QueryValue(refreshUrl, "dfid") == device.dfid &&
      QueryValue(refreshUrl, "mid") == device.mid &&
      QueryValue(refreshUrl, "uuid") == "-" &&
      QueryValue(refreshUrl, "userid") == "42" &&
      QueryValue(refreshUrl, "token") == "old-token" &&
      QueryValue(refreshUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              signedParams, refreshBody, echo::core::KuGouSaltKind::Lite) &&
      echo::core::SignatureAndroidParams(
          signedParams, refreshBody, echo::core::KuGouSaltKind::Standard) !=
          QueryValue(refreshUrl, "signature") &&
      wireBody.value("dfid", std::string{}) == device.dfid &&
      wireBody.value("userid", 0) == 42 &&
      !wireBody.value("t1", std::string{}).empty() &&
      !wireBody.value("t2", std::string{}).empty() &&
      wireBody.value("dev", std::string{}) == device.serverDev &&
      isLowerHex(wireBody.value("p3", std::string{})) &&
      isLowerHex(wireBody.value("params", std::string{})) &&
      refreshHeaders["clienttime"] == clienttime &&
      refreshHeaders["dfid"] == device.dfid &&
      refreshHeaders["mid"] == device.mid &&
      refreshHeaders["x-router"] == "login.user.kugou.com" &&
      refreshHeaders["kg-rc"] == "1" &&
      refreshHeaders["kg-thash"] == "5d816a0" &&
      refreshHeaders["kg-rec"] == "1" &&
      refreshHeaders["kg-rf"] == "B9EDA08A64250DEFFBCADDEE00F8F25F";
  if (!refreshMatchesReference) {
    std::cerr << "[LoginContract] login_by_token must be ONE Lite-family refresh "
              << "(appid=3116, body carries t1/t2/dev); calls=" << refreshCalls
              << " appid=" << QueryValue(refreshUrl, "appid")
              << " clientver=" << QueryValue(refreshUrl, "clientver") << std::endl;
    return 1;
  }

  // An unregistered device cannot satisfy the Lite contract, so the refresh is
  // skipped rather than degraded to the family that mints a credential the
  // claims cannot use. Zero requests, no credential.
  {
    int unregisteredCalls = 0;
    auto idleDevice = device;
    idleDevice.registered = false;
    echo::core::LoginService unregisteredService(
        [](const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "{}", "unexpected GET"};
        },
        [&](const std::string&,
            const std::string&,
            const std::unordered_map<std::string, std::string>&) {
          ++unregisteredCalls;
          return echo::core::HttpResult{
              200, R"({"status":1,"data":{"token":"stale","vip_token":"stale-vip"}})", ""};
        });
    const auto skipped = unregisteredService.RefreshSession(
        idleDevice, "42", "old-token", "old-t1");
    if (skipped || unregisteredCalls != 0) {
      std::cerr << "[LoginContract] unregistered device must skip the refresh "
                << "without issuing any request: calls=" << unregisteredCalls
                << " produced_credential=" << (skipped ? "Y" : "N") << std::endl;
      return 1;
    }
  }

  std::cout << "[LoginContract] Testing single Lite refresh wire contract..." << std::endl;
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
        return echo::core::HttpResult{
            200,
            R"({"status":1,"data":{"token":"concept-rotated","vip_token":"concept-vip-token","vip_type":3,"t1":"concept-t1"}})",
            ""};
      });

  const auto conceptRefreshed = fallbackService.RefreshSession(
      conceptDevice, "42", "old-token", "old-t1");
  if (!conceptRefreshed || fallbackUrls.size() != 1 ||
      conceptRefreshed->token != "concept-rotated" ||
      conceptRefreshed->vipToken != "concept-vip-token" ||
      conceptRefreshed->vipType != 3 || conceptRefreshed->t1 != "concept-t1") {
    std::cerr << "[LoginContract] the single Lite refresh did not preserve the VIP session "
              << "(urls=" << fallbackUrls.size() << ")" << std::endl;
    return 1;
  }

  const auto conceptBody = nlohmann::json::parse(fallbackBodies[0]);
  const auto conceptClienttime = QueryValue(fallbackUrls[0], "clienttime");
  const std::unordered_map<std::string, std::string> conceptSignedParams = {
      {"appid", QueryValue(fallbackUrls[0], "appid")},
      {"clientver", QueryValue(fallbackUrls[0], "clientver")},
      {"clienttime", conceptClienttime},
      {"dfid", QueryValue(fallbackUrls[0], "dfid")},
      {"mid", QueryValue(fallbackUrls[0], "mid")},
      {"uuid", QueryValue(fallbackUrls[0], "uuid")},
      {"userid", QueryValue(fallbackUrls[0], "userid")},
      {"token", QueryValue(fallbackUrls[0], "token")},
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
      QueryValue(fallbackUrls[0], "appid") == "3116" &&
      QueryValue(fallbackUrls[0], "clientver") == "11440" &&
      QueryValue(fallbackUrls[0], "dfid") == conceptDevice.dfid &&
      QueryValue(fallbackUrls[0], "token") == "old-token" &&
      QueryValue(fallbackUrls[0], "signature") ==
          echo::core::SignatureAndroidParams(
              conceptSignedParams, fallbackBodies[0], echo::core::KuGouSaltKind::Lite) &&
      conceptBody.value("dev", std::string{}) == conceptDevice.serverDev &&
      conceptP3.value("token", std::string{}) == "old-token" &&
      conceptT1 == "old-t1|" + conceptMs &&
      conceptT2 == conceptDevice.guid +
          "|0f607264fc6318a92b9e13c65db7cd3c|" + conceptDevice.mac + "|" +
          conceptDevice.serverDev + "|" + conceptMs &&
      fallbackHeaders[0].at("dfid") == conceptDevice.dfid;
  if (!conceptMatchesReference) {
    std::cerr << "[LoginContract] the Lite refresh does not match the lite contract"
              << std::endl;
    return 1;
  }

  // Regression guard for the ordering itself. The block this replaces asserted
  // "concept token was retried after standard rejection" — i.e. it pinned the
  // two-attempt order as desirable. A2 removes the Standard attempt, so what
  // has to hold instead is that the FIRST login_by_token is already the Lite
  // family: one call, appid=3116, and a rejection on it produces no credential
  // rather than triggering another family.
  int conceptOnlyCalls = 0;
  std::vector<std::string> conceptOnlyUrls;
  echo::core::LoginService conceptOnlyService(
      [](const std::string&,
         const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "{}", "unexpected GET"};
      },
      [&](const std::string& url,
          const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        ++conceptOnlyCalls;
        conceptOnlyUrls.push_back(url);
        return echo::core::HttpResult{
            200, R"({"status":0,"data":null,"error_code":20018})", ""};
      });
  const auto conceptOnly = conceptOnlyService.RefreshSession(
      conceptDevice, "42", "concept-login-token", "concept-login-t1");
  if (conceptOnly || conceptOnlyCalls != 1 ||
      QueryValue(conceptOnlyUrls[0], "appid") != "3116") {
    std::cerr << "[LoginContract] the first refresh must already be the Lite family "
              << "and must not retry another: calls=" << conceptOnlyCalls
              << " appid=" << QueryValue(conceptOnlyUrls[0], "appid")
              << " produced_credential=" << (conceptOnly ? "Y" : "N") << std::endl;
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
  // rotation. Persist the rotation as a whole session rather than dropping it
  // because the credential came back empty.
  //
  // This used to loop over {standard-first-then-concept, concept-only} to prove
  // the rotation survived either starting point. With one family there is no
  // starting point to vary, so the axis is gone; the field-by-field check stays
  // and intentionally overlaps B1(b) below, which asserts the same bundle from
  // the ordering angle rather than the entitlement angle.
  {
    int calls = 0;
    echo::core::LoginService rotationService({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"concept-new","vip_token":"","vip_type":0,"t1":"concept-new-t1"}})", ""};
    });
    const auto rotated = rotationService.RefreshSession(conceptDevice, "42", "old-token", "old-t1");
    if (!rotated || calls != 1 || rotated->token != "concept-new" ||
        rotated->t1 != "concept-new-t1" || !rotated->vipToken.empty() || rotated->vipType != 0) {
      std::cerr << "[LoginContract] successful rotation without VIP was discarded "
                << "(calls=" << calls << ")" << std::endl;
      return 1;
    }
  }

  // B1: the single-family ordering, each branch identified by the returned
  // credential bundle. A successful rotation must never be reverted to an
  // older token, and a rejected rotation must never invent entitlement. There
  // used to be a third branch here — "Standard rotated the token but the
  // Concept retry failed, so keep the Standard rotation". It cannot exist any
  // more, and that is the point: keeping a token rotated by a family whose
  // claims are rejected is exactly the state that looked like a working login.
  {
    // (a) The session-family refresh issues the credential: one call, and its
    // bundle is what the caller receives.
    int calls = 0;
    echo::core::LoginService sessionFamilyVip({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"lite-token","vip_token":"lite-vip","vip_type":2,"t1":"lite-t1"}})", ""};
    });
    const auto result = sessionFamilyVip.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (!result || calls != 1 || result->token != "lite-token" ||
        result->vipToken != "lite-vip" || result->vipType != 2 || result->t1 != "lite-t1") {
      std::cerr << "[LoginContract] session-family refresh did not return its bundle after "
                << "exactly one call (calls=" << calls << ")" << std::endl;
      return 1;
    }
  }
  {
    // (b) A rotation that succeeds but grants no VIP is still kept: rotating
    // the ordinary token is real progress, and discarding it would revive a
    // credential the upstream may already have invalidated.
    int calls = 0;
    echo::core::LoginService noVip({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200,
        R"({"status":1,"data":{"token":"rot-no-vip","vip_token":"","vip_type":0,"t1":"rot-t1"}})", ""};
    });
    const auto result = noVip.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (!result || calls != 1 || result->token != "rot-no-vip" || result->t1 != "rot-t1" ||
        !result->vipToken.empty() || result->vipType != 0) {
      std::cerr << "[LoginContract] rotation without VIP was discarded (calls=" << calls << ")"
                << std::endl;
      return 1;
    }
  }
  {
    // (c) Rejected: no credential is produced, so the caller keeps the
    // existing session untouched, and no second family is tried after it.
    int calls = 0;
    echo::core::LoginService bothFail({}, [&](const auto&, const auto&, const auto&) {
      ++calls;
      return echo::core::HttpResult{200, R"({"status":0,"error_code":20018})", ""};
    });
    const auto result = bothFail.RefreshSession(conceptDevice, "42", "old", "old-t1");
    if (result || calls != 1) {
      std::cerr << "[LoginContract] rejected refresh produced a credential or retried "
                << "through another family (calls=" << calls << ")" << std::endl;
      return 1;
    }
  }

  std::cout << "[LoginContract] passed" << std::endl;
  return 0;
}

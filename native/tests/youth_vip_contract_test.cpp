#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/UserService.h"

namespace {

std::string QueryValue(const std::string& url, const std::string& key) {
  const auto marker = key + "=";
  const auto start = url.find(marker);
  if (start == std::string::npos) return {};
  const auto valueStart = start + marker.size();
  const auto end = url.find('&', valueStart);
  return url.substr(valueStart, end == std::string::npos ? std::string::npos : end - valueStart);
}

std::vector<std::string> QueryKeys(const std::string& url) {
  std::vector<std::string> keys;
  const auto q = url.find('?');
  if (q == std::string::npos) return keys;
  std::string rest = url.substr(q + 1);
  while (!rest.empty()) {
    const auto amp = rest.find('&');
    const auto pair = rest.substr(0, amp);
    const auto eq = pair.find('=');
    keys.push_back(eq == std::string::npos ? pair : pair.substr(0, eq));
    if (amp == std::string::npos) break;
    rest = rest.substr(amp + 1);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::vector<std::string> HeaderKeys(
    const std::unordered_map<std::string, std::string>& headers) {
  std::vector<std::string> keys;
  keys.reserve(headers.size());
  for (const auto& [k, _] : headers) keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::string Join(const std::vector<std::string>& values) {
  std::ostringstream out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out << ',';
    out << values[i];
  }
  return out.str();
}

void PrintCapture(const char* name, const std::string& method, const std::string& url,
                  const std::string& body,
                  const std::unordered_map<std::string, std::string>& headers) {
  const auto appid = QueryValue(url, "appid");
  const auto clientver = QueryValue(url, "clientver");
  const bool cookie = headers.find("Cookie") != headers.end();
  std::cout << "[CAPTURE] name=" << name
            << " method=" << method
            << " endpoint=" << url.substr(0, url.find('?'))
            << " appid=" << appid
            << " clientver=" << clientver
            << " uuid=" << QueryValue(url, "uuid")
            << " token_len=" << QueryValue(url, "token").size()
            << " userid_present=" << (QueryValue(url, "userid").empty() ? "N" : "Y")
            << " param_keys=" << Join(QueryKeys(url))
            << " header_keys=" << Join(HeaderKeys(headers))
            << " cookie_header=" << (cookie ? "Y" : "N")
            << " header_dfid=" << (headers.count("dfid") ? "Y" : "N")
            << " header_mid=" << (headers.count("mid") ? "Y" : "N")
            << " header_kg-rf=" << (headers.count("kg-rf") ? "Y" : "N")
            << " body_len=" << body.size()
            << " content_type="
            << (headers.count("Content-Type") ? headers.at("Content-Type") : "")
            << std::endl;
}

}  // namespace

int main() {
  std::cout << "[YouthVipContract] matches the registered Android request contract" << std::endl;

  std::string capturedUrl;
  std::string capturedBody;
  std::unordered_map<std::string, std::string> capturedHeaders;

  echo::core::UserService service([&](
      const std::string& url,
      const std::string& body,
      const std::unordered_map<std::string, std::string>& headers) {
    capturedUrl = url;
    capturedBody = body;
    capturedHeaders = headers;
    return echo::core::HttpResult{
        200,
        R"({"status":0,"error_code":51002,"message":"device validation failed"})",
        ""};
  });

  echo::core::DeviceInfo device;
  device.dfid = "abcdefghijklmnopqrstuvwx";
  device.mid = "123456789012345678901234567890123456789";
  device.uuid = "0123456789abcdef0123456789abcdef";
  device.guid = "12345678-1234-1234-1234-123456789abc";

  {
    auto capturePost = [&](const char* name, auto call) {
      std::string url, body;
      std::unordered_map<std::string, std::string> headers;
      echo::core::UserService svc(
          [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
            return echo::core::HttpResult{500, "", "unexpected GET"};
          },
          [&](const std::string& u, const std::string& b,
              const std::unordered_map<std::string, std::string>& h) {
            url = u;
            body = b;
            headers = h;
            return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
          });
      call(svc);
      PrintCapture(name, "POST", url, body, headers);
    };
    auto captureGet = [&](const char* name, echo::core::KuGouEdition edition) {
      std::string url;
      std::unordered_map<std::string, std::string> headers;
      echo::core::UserService svc(
          [&](const std::string& u, const std::unordered_map<std::string, std::string>& h) {
            url = u;
            headers = h;
            return echo::core::HttpResult{200, R"({"status":1,"data":{"is_vip":0}})", ""};
          },
          [&](const std::string&, const std::string&,
              const std::unordered_map<std::string, std::string>&) {
            return echo::core::HttpResult{500, "", "unexpected POST"};
          });
      svc.GetUserVip(device, "42", "token", edition);
      PrintCapture(name, "GET", url, "", headers);
    };
    capturePost("claim_day", [&](echo::core::UserService& svc) {
      svc.ClaimVip(device, "42", "token");
    });
    capturePost("claim_upgrade", [&](echo::core::UserService& svc) {
      svc.UpgradeVipReward(device, "42", "token");
    });
    capturePost("claim_listen", [&](echo::core::UserService& svc) {
      svc.ClaimYouthListenSong(device, "42", "token");
    });
    capturePost("claim_ad", [&](echo::core::UserService& svc) {
      svc.ClaimYouthAdVip(device, "42", "token");
    });
    // Both families captured explicitly: this block only feeds PrintCapture's
    // diagnostic dump, it asserts nothing. The DEFAULT-value check for
    // GetUserVip lives below, on a call that passes no edition argument.
    captureGet("vip_query_standard_explicit", echo::core::KuGouEdition::Standard);
    captureGet("vip_query_concept_explicit", echo::core::KuGouEdition::Concept);
  }

  const auto result = service.ClaimYouthListenSong(device, "42", "token");

  // Every youth claim and VIP query follows the family that MINTED THE SESSION
  // (kProjectEdition), because the QR poll in LoginService mints the session
  // with exactly that profile. A Standard-signed call carries a signature the
  // Concept-minted session cannot authenticate, which is what upstream reports
  // back as 51002 on every claim. Reference server/module/youth_listen_song.js
  // is the same shape: appid and salt stay lite, only clientver is overridden.
  const auto productionClaim = echo::core::GetKuGouProfile(echo::core::kProjectEdition);
  const auto clienttime = QueryValue(capturedUrl, "clienttime");
  const std::unordered_map<std::string, std::string> signedParams = {
      {"appid", QueryValue(capturedUrl, "appid")},
      {"clientver", QueryValue(capturedUrl, "clientver")},
      {"clienttime", clienttime},
      {"dfid", QueryValue(capturedUrl, "dfid")},
      {"mid", QueryValue(capturedUrl, "mid")},
      {"uuid", QueryValue(capturedUrl, "uuid")},
      {"userid", QueryValue(capturedUrl, "userid")},
      {"token", QueryValue(capturedUrl, "token")},
  };
  const bool requestMatchesReference =
      capturedUrl.find("https://gateway.kugou.com/youth/v2/report/listen_song?") == 0 &&
      QueryValue(capturedUrl, "appid") == productionClaim.appid &&
      QueryValue(capturedUrl, "clientver") == "10566" &&
      QueryValue(capturedUrl, "dfid") == device.dfid &&
      QueryValue(capturedUrl, "mid") == device.mid &&
      QueryValue(capturedUrl, "uuid") == "-" &&
      QueryValue(capturedUrl, "userid") == "42" &&
      QueryValue(capturedUrl, "token") == "token" &&
      QueryValue(capturedUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              signedParams, capturedBody, productionClaim.saltKind) &&
      capturedBody == R"({"mixsongid":666075191})" &&
      !clienttime.empty() &&
      capturedHeaders["clienttime"] == clienttime &&
      capturedHeaders["dfid"] == device.dfid &&
      capturedHeaders["mid"] == device.mid &&
      capturedHeaders["kg-rc"] == "1" &&
      capturedHeaders["kg-thash"] == "5d816a0" &&
      capturedHeaders["kg-rec"] == "1" &&
      capturedHeaders["kg-rf"] == "B9EDA08A64250DEFFBCADDEE00F8F25F" &&
      capturedHeaders["User-Agent"] ==
          "Android13-1070-10566-201-0-ReportPlaySongToServerProtocol-wifi";

  if (!requestMatchesReference) {
    std::cerr << "[YouthVipContract] listen request must use the session family "
              << "(kProjectEdition appid=" << productionClaim.appid
              << " + " << echo::core::KuGouSaltKindName(productionClaim.saltKind) << " salt, "
              << "clientver override 10566); "
              << "got appid=" << QueryValue(capturedUrl, "appid")
              << " clientver=" << QueryValue(capturedUrl, "clientver") << std::endl;
    return 1;
  }

  if (result.value("status", -1) != 0 ||
      result.value("error_code", -1) != 51002 ||
      result.value("error_msg", std::string{}) != "device validation failed" ||
      result.value("error", std::string{}) != "device validation failed") {
    std::cerr << "[YouthVipContract] unexpected normalized response: "
              << result.dump() << std::endl;
    return 1;
  }

  std::cout << "[YouthVipContract] listen passed" << std::endl;

  std::string vipUrl;
  std::unordered_map<std::string, std::string> vipHeaders;
  echo::core::UserService vipService(
      [&](const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        vipUrl = url;
        vipHeaders = headers;
        return echo::core::HttpResult{200, R"({"status":1,"data":{"is_vip":0}})", ""};
      },
      [&](const std::string&, const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected POST"};
      });

  vipService.GetUserVip(device, "42", "token");

  const auto vipClienttime = QueryValue(vipUrl, "clienttime");
  const std::unordered_map<std::string, std::string> vipSignedParams = {
      {"appid", QueryValue(vipUrl, "appid")},
      {"clientver", QueryValue(vipUrl, "clientver")},
      {"clienttime", vipClienttime},
      {"dfid", QueryValue(vipUrl, "dfid")},
      {"mid", QueryValue(vipUrl, "mid")},
      {"uuid", QueryValue(vipUrl, "uuid")},
      {"userid", QueryValue(vipUrl, "userid")},
      {"token", QueryValue(vipUrl, "token")},
      {"busi_type", QueryValue(vipUrl, "busi_type")},
  };
  const auto cookie = vipHeaders.count("Cookie") ? vipHeaders.at("Cookie") : std::string{};
  const bool cookieHasSessionKeys =
      cookie.find("token=") != std::string::npos &&
      cookie.find("userid=") != std::string::npos &&
      cookie.find("KugooID=") != std::string::npos;
  const bool vipRequestMatchesReference =
      vipUrl.find("https://kugouvip.kugou.com/v1/get_union_vip?") == 0 &&
      QueryValue(vipUrl, "appid") == productionClaim.appid &&
      QueryValue(vipUrl, "clientver") == productionClaim.clientver &&
      QueryValue(vipUrl, "busi_type") == "concept" &&
      QueryValue(vipUrl, "uuid") == "-" &&
      QueryValue(vipUrl, "product_type").empty() &&
      QueryValue(vipUrl, "opt_product_types").empty() &&
      QueryValue(vipUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              vipSignedParams, "", productionClaim.saltKind) &&
      !vipClienttime.empty() && cookieHasSessionKeys;

  if (!vipRequestMatchesReference) {
    std::cerr << "[YouthVipContract] GetUserVip() default must be kProjectEdition "
              << "(appid=" << productionClaim.appid
              << "); got appid=" << QueryValue(vipUrl, "appid")
              << " clientver=" << QueryValue(vipUrl, "clientver")
              << ". A Standard default here is a trap: the route layer already "
              << "injects Concept explicitly, so a green route test cannot see "
              << "any caller that silently takes the default." << std::endl;
    return 1;
  }

  // ── Day claim contract: receive_vip_listen_song uses the session family ──
  // The daily claim is the endpoint that returned 51002 for two months. Params
  // stay in the query with an empty body; fingerprint headers stay as they
  // are. uuid/headers/device are not part of the family switch.
  std::string dayUrl;
  std::string dayBody = "UNSET";
  std::unordered_map<std::string, std::string> dayHeaders;
  echo::core::UserService dayService(
      [&](const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string& url, const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        dayUrl = url;
        dayBody = body;
        dayHeaders = headers;
        return echo::core::HttpResult{
            200, R"({"status":0,"error_code":51002,"error_msg":"device validation failed"})", ""};
      });

  const auto dayResult = dayService.ClaimVip(device, "42", "token");

  const auto dayClienttime = QueryValue(dayUrl, "clienttime");
  const auto receiveDay = QueryValue(dayUrl, "receive_day");
  const std::unordered_map<std::string, std::string> daySignedParams = {
      {"appid", QueryValue(dayUrl, "appid")},
      {"clientver", QueryValue(dayUrl, "clientver")},
      {"clienttime", dayClienttime},
      {"dfid", QueryValue(dayUrl, "dfid")},
      {"mid", QueryValue(dayUrl, "mid")},
      {"uuid", QueryValue(dayUrl, "uuid")},
      {"userid", QueryValue(dayUrl, "userid")},
      {"token", QueryValue(dayUrl, "token")},
      {"source_id", QueryValue(dayUrl, "source_id")},
      {"receive_day", receiveDay},
  };
  const bool dayMatchesReference =
      dayUrl.find("https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song?") == 0 &&
      QueryValue(dayUrl, "appid") == productionClaim.appid &&
      QueryValue(dayUrl, "clientver") == productionClaim.clientver &&
      QueryValue(dayUrl, "dfid") == device.dfid &&
      QueryValue(dayUrl, "mid") == device.mid &&
      QueryValue(dayUrl, "uuid") == "-" &&
      QueryValue(dayUrl, "userid") == "42" &&
      QueryValue(dayUrl, "token") == "token" &&
      QueryValue(dayUrl, "source_id") == "90139" &&
      QueryValue(dayUrl, "plat").empty() &&
      receiveDay.size() == 10 && receiveDay[4] == '-' && receiveDay[7] == '-' &&
      QueryValue(dayUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              daySignedParams, "", productionClaim.saltKind) &&
      dayBody.empty() &&
      dayHeaders["Content-Type"] == "application/x-www-form-urlencoded" &&
      dayHeaders["clienttime"] == dayClienttime &&
      dayHeaders["dfid"] == device.dfid &&
      dayHeaders["mid"] == device.mid &&
      dayHeaders["kg-rc"] == "1" &&
      dayHeaders["kg-thash"] == "5d816a0" &&
      dayHeaders["kg-rec"] == "1" &&
      dayHeaders["kg-rf"] == "B9EDA08A64250DEFFBCADDEE00F8F25F" &&
      dayHeaders["User-Agent"] ==
          "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";

  if (!dayMatchesReference) {
    std::cerr << "[YouthVipContract] receive_vip_listen_song must use the session "
              << "family (appid=" << productionClaim.appid
              << " clientver=" << productionClaim.clientver
              << " + " << echo::core::KuGouSaltKindName(productionClaim.saltKind) << " salt"
              << "); got appid=" << QueryValue(dayUrl, "appid")
              << " clientver=" << QueryValue(dayUrl, "clientver") << std::endl;
    return 1;
  }

  if (dayResult.value("status", -1) != 0 ||
      dayResult.value("error_code", -1) != 51002 ||
      dayResult.value("error_msg", std::string{}) != "device validation failed" ||
      dayResult.value("local_error", std::string{}) != "kugou_vip_claim_failed") {
    std::cerr << "[YouthVipContract] unexpected ClaimVip normalized response: "
              << dayResult.dump() << std::endl;
    return 1;
  }

  std::cout << "[YouthVipContract] day-vip session-family claim contract passed"
            << std::endl;

  // The day claim and the VIP query are now the SAME family, which is the
  // point of the change: the query route already injected Concept, so a
  // Standard day claim was signing with an identity the session that had just
  // answered that same query could not recognise.
  if (QueryValue(dayUrl, "appid") != QueryValue(vipUrl, "appid") ||
      QueryValue(dayUrl, "clientver") != QueryValue(vipUrl, "clientver")) {
    std::cerr << "[YouthVipContract] day claim and VIP query must share one family; got "
              << "claim appid=" << QueryValue(dayUrl, "appid")
              << " query appid=" << QueryValue(vipUrl, "appid") << std::endl;
    return 1;
  }

  std::string upgradeUrl;
  std::string upgradeBody = "UNSET";
  std::unordered_map<std::string, std::string> upgradeHeaders;
  echo::core::UserService upgradeService(
      [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string& url, const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        upgradeUrl = url;
        upgradeBody = body;
        upgradeHeaders = headers;
        return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
      });
  upgradeService.UpgradeVipReward(device, "42", "token");
  const auto upgradeClienttime = QueryValue(upgradeUrl, "clienttime");
  const std::unordered_map<std::string, std::string> upgradeSignedParams = {
      {"appid", QueryValue(upgradeUrl, "appid")},
      {"clientver", QueryValue(upgradeUrl, "clientver")},
      {"clienttime", upgradeClienttime},
      {"dfid", QueryValue(upgradeUrl, "dfid")},
      {"mid", QueryValue(upgradeUrl, "mid")},
      {"uuid", QueryValue(upgradeUrl, "uuid")},
      {"userid", QueryValue(upgradeUrl, "userid")},
      {"token", QueryValue(upgradeUrl, "token")},
      {"kugouid", QueryValue(upgradeUrl, "kugouid")},
      {"ad_type", QueryValue(upgradeUrl, "ad_type")},
  };
  const bool upgradeMatchesProduction =
      upgradeUrl.find("https://gateway.kugou.com/youth/v1/listen_song/upgrade_vip_reward?") == 0 &&
      QueryValue(upgradeUrl, "appid") == productionClaim.appid &&
      QueryValue(upgradeUrl, "clientver") == productionClaim.clientver &&
      QueryValue(upgradeUrl, "uuid") == "-" &&
      QueryValue(upgradeUrl, "ad_type") == "1" &&
      QueryValue(upgradeUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              upgradeSignedParams, "", productionClaim.saltKind) &&
      upgradeBody.empty() &&
      upgradeHeaders.count("dfid") == 1 &&
      upgradeHeaders.count("kg-rf") == 1;
  if (!upgradeMatchesProduction) {
    std::cerr << "[YouthVipContract] upgrade_vip_reward must use the session family; "
              << "got appid=" << QueryValue(upgradeUrl, "appid")
              << " clientver=" << QueryValue(upgradeUrl, "clientver") << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] upgrade session-family claim contract passed"
            << std::endl;

  std::string adUrl;
  std::string adBody;
  std::unordered_map<std::string, std::string> adHeaders;
  echo::core::UserService adService(
      [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string& url, const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        adUrl = url;
        adBody = body;
        adHeaders = headers;
        return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
      });
  adService.ClaimYouthAdVip(device, "42", "token");
  const auto adClienttime = QueryValue(adUrl, "clienttime");
  const std::unordered_map<std::string, std::string> adSignedParams = {
      {"appid", QueryValue(adUrl, "appid")},
      {"clientver", QueryValue(adUrl, "clientver")},
      {"clienttime", adClienttime},
      {"dfid", QueryValue(adUrl, "dfid")},
      {"mid", QueryValue(adUrl, "mid")},
      {"uuid", QueryValue(adUrl, "uuid")},
      {"userid", QueryValue(adUrl, "userid")},
      {"token", QueryValue(adUrl, "token")},
  };
  // The ad report follows the session family like every other youth endpoint.
  // Two further diffs vs the local reference are deliberately NOT touched here
  // because they are not part of the family switch: uuid falls through to
  // device.guid (the ad report does not force uuid="-"), and this route omits
  // the public device fingerprint headers. Both remain investigation notes,
  // not claimed 51002 root causes.
  const bool adMatchesSessionFamily =
      adUrl.find("https://gateway.kugou.com/youth/v1/ad/play_report?") == 0 &&
      QueryValue(adUrl, "appid") == productionClaim.appid &&
      QueryValue(adUrl, "clientver") == productionClaim.clientver &&
      QueryValue(adUrl, "uuid") == device.guid &&
      QueryValue(adUrl, "signature") ==
          echo::core::SignatureAndroidParams(adSignedParams, adBody, productionClaim.saltKind) &&
      adBody.find("\"ad_id\":12307537187") != std::string::npos &&
      adHeaders.count("dfid") == 0 &&
      adHeaders.count("kg-rf") == 0;
  if (!adMatchesSessionFamily) {
    std::cerr << "[YouthVipContract] ad play_report must use the session family; "
              << "got appid=" << QueryValue(adUrl, "appid")
              << " clientver=" << QueryValue(adUrl, "clientver")
              << " uuid=" << QueryValue(adUrl, "uuid") << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] ad session-family report contract passed"
            << std::endl;

  // ── Session-family profile TABLE pin, and the 1005 anti-regression scan ──
  // The request paths above are all asserted against GetKuGouProfile() output,
  // so a table drift could otherwise move the goalposts silently. This block
  // pins the literal identity that mints the session, then scans every captured
  // youth request for the standard-family appid. The scan is the guard against
  // a regression of exactly the defect this change removes: 51002 on every
  // claim for two months because four endpoints silently kept appid=1005.
  const auto sessionProfile =
      echo::core::GetKuGouProfile(echo::core::kProjectEdition);
  if (sessionProfile.appid != "3116" ||
      sessionProfile.clientver != "11440" ||
      sessionProfile.saltKind != echo::core::KuGouSaltKind::Lite) {
    std::cerr << "[YouthVipContract] session profile table drifted: appid="
              << sessionProfile.appid
              << " clientver=" << sessionProfile.clientver
              << " salt=" << echo::core::KuGouSaltKindName(sessionProfile.saltKind)
              << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] session profile table pin appid="
            << sessionProfile.appid
            << " clientver=" << sessionProfile.clientver
            << " salt=" << echo::core::KuGouSaltKindName(sessionProfile.saltKind)
            << std::endl;

  // No youth endpoint may sign with the standard family, whatever the caller
  // passes. Covers all four claims plus the entitlement query.
  const std::unordered_map<std::string, std::string> youthRequests = {
      {"listen_song", capturedUrl},
      {"get_union_vip", vipUrl},
      {"receive_vip_listen_song", dayUrl},
      {"upgrade_vip_reward", upgradeUrl},
      {"ad/play_report", adUrl},
  };
  for (const auto& [name, url] : youthRequests) {
    if (url.empty()) {
      std::cerr << "[YouthVipContract] no URL captured for " << name << std::endl;
      return 1;
    }
    const auto appid = QueryValue(url, "appid");
    if (appid != productionClaim.appid) {
      std::cerr << "[YouthVipContract] " << name << " must never carry the standard "
                << "family: appid=" << appid
                << ", expected " << productionClaim.appid << std::endl;
      return 1;
    }
  }
  std::cout << "[YouthVipContract] no youth endpoint carries appid=1005 (anti-regression pin passed)"
            << std::endl;

  echo::core::UserService empty51002(
      [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string&, const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
      });
  const auto empty51002Result = empty51002.ClaimVip(device, "42", "token");
  const auto empty51002Msg = empty51002Result.value("error_msg", std::string{});
  const auto empty51002Error = empty51002Result.value("error", std::string{});
  const auto empty51002Fallback =
      empty51002Result.value("local_fallback_message", std::string{});
  if (empty51002Result.value("status", -1) != 0 ||
      empty51002Result.value("error_code", -1) != 51002 ||
      !empty51002Msg.empty() ||
      empty51002Fallback != "上游拒绝，原因未明确" ||
      empty51002Error != "上游拒绝，原因未明确" ||
      empty51002Error.find("SDK") != std::string::npos ||
      empty51002Error.find("广告") != std::string::npos) {
    std::cerr << "[YouthVipContract] empty 51002 must keep upstream message empty "
              << "and use a local fallback without inventing SDK: "
              << empty51002Result.dump() << std::endl;
    return 1;
  }

  echo::core::UserService adEmpty51002(
      [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string&, const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{200, R"({"status":0,"error_code":51002})", ""};
      });
  const auto adEmpty = adEmpty51002.ClaimYouthAdVip(device, "42", "token");
  const auto adMsg = adEmpty.value("error_msg", std::string{});
  const auto adError = adEmpty.value("error", std::string{});
  if (adEmpty.value("status", -1) != 0 ||
      adEmpty.value("error_code", -1) != 51002 ||
      !adMsg.empty() ||
      adEmpty.value("local_fallback_message", std::string{}) != "上游拒绝，原因未明确" ||
      adError.find("SDK") != std::string::npos) {
    std::cerr << "[YouthVipContract] ad empty 51002 must not invent SDK credentials: "
              << adEmpty.dump() << std::endl;
    return 1;
  }

  // ── A4: machine-readable claim failure classification ────────────────────
  // The frontend must not guess a user-facing sentence from a bare upstream
  // code, and the native layer must not invent one either. Each code maps to a
  // `reason` plus a `reason_confidence` saying whether that meaning was
  // observed in this account's own log or merely inferred. The distinction is
  // load-bearing: the 09-03 "Concept is rejected too" conclusion was an
  // inference recorded as if it had been observed, and it redirected every
  // endpoint onto the wrong family for three weeks.
  struct ReasonCase {
    int code;
    const char* reason;
    const char* confidence;
  };
  const ReasonCase reasonCases[] = {
      {131001, "already_claimed_today", "observed"},
      {297002, "upgrade_already_claimed", "observed"},
      {51002, "rejected_identity_or_risk", "observed"},
      {130012, "listen_song_not_eligible", "inferred"},
      {304001, "bad_receive_day", "observed"},
      {20017, "session_family_mismatch", "observed"},
      {20018, "session_family_mismatch", "observed"},
  };
  for (const auto& rc : reasonCases) {
    echo::core::UserService svc(
        [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "", "unexpected GET"};
        },
        [rc](const std::string&, const std::string&,
             const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200, R"({"status":0,"error_code":)" + std::to_string(rc.code) +
                   R"(,"error_msg":"upstream text"})", ""};
        });
    const auto out = svc.ClaimVip(device, "42", "token");
    // Presence is asserted before value: a missing key and an empty value read
    // the same through value() and would let the contract regress silently.
    if (!out.contains("reason") || !out.contains("reason_confidence")) {
      std::cerr << "[YouthVipContract] failure for " << rc.code
                << " carries no reason field: " << out.dump() << std::endl;
      return 1;
    }
    if (out.value("reason", std::string{}) != rc.reason ||
        out.value("reason_confidence", std::string{}) != rc.confidence) {
      std::cerr << "[YouthVipContract] code " << rc.code << " classified as "
                << out.value("reason", std::string{}) << "/"
                << out.value("reason_confidence", std::string{}) << ", expected "
                << rc.reason << "/" << rc.confidence << std::endl;
      return 1;
    }
  }

  // An unrecognised code must not be folded into a known bucket, and must keep
  // the upstream text so the UI can show what actually came back.
  {
    echo::core::UserService svc(
        [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "", "unexpected GET"};
        },
        [](const std::string&, const std::string&,
           const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200, R"({"status":0,"error_code":999999,"error_msg":"never seen before"})", ""};
        });
    const auto out = svc.ClaimVip(device, "42", "token");
    if (out.value("reason", std::string{}) != "unknown" ||
        out.value("reason_confidence", std::string{}) != "none" ||
        out.value("error_code", -1) != 999999 ||
        out.value("error_msg", std::string{}) != "never seen before") {
      std::cerr << "[YouthVipContract] unknown code must stay unknown and keep the "
                   "upstream text: " << out.dump() << std::endl;
      return 1;
    }
  }

  // The classification must hold on every claim route, not just the day claim:
  // upgrade, listen-song and ad build their responses along different paths.
  {
    struct { const char* name; int code; } routes[] = {
        {"upgrade", 297002}, {"listen", 130012}, {"ad", 51002},
    };
    for (const auto& r : routes) {
      echo::core::UserService svc(
          [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
            return echo::core::HttpResult{500, "", "unexpected GET"};
          },
          [&r](const std::string&, const std::string&,
               const std::unordered_map<std::string, std::string>&) {
            return echo::core::HttpResult{
                200, R"({"status":0,"error_code":)" + std::to_string(r.code) + "}", ""};
          });
      nlohmann::json out =
          std::string(r.name) == "upgrade" ? svc.UpgradeVipReward(device, "42", "token")
          : std::string(r.name) == "listen" ? svc.ClaimYouthListenSong(device, "42", "token")
                                            : svc.ClaimYouthAdVip(device, "42", "token");
      if (!out.contains("reason") || !out.contains("reason_confidence")) {
        std::cerr << "[YouthVipContract] " << r.name
                  << " route carries no reason field: " << out.dump() << std::endl;
        return 1;
      }
    }
  }
  std::cout << "[YouthVipContract] claim reason classification contract passed" << std::endl;

  // ── A5: entitlement tier derived from busi_vip ───────────────────────────
  // Two real shapes from this account, synthetic in every field but structure:
  // 09-29 (daily claim only: tvip active, svip expired) and 09-24 (after the
  // upgrade: svip active). The expiry that drives `active` uses a far-future
  // literal rather than "now plus n", so the case cannot start failing because
  // somebody ran the suite on a different clock.
  const auto vipWithBusiList = [&device](const std::string& busiVipJson) {
    echo::core::UserService svc(
        [&](const std::string&, const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{
              200, R"({"status":1,"data":{"is_vip":0,"busi_vip":)" + busiVipJson + R"(}})", ""};
        },
        [&](const std::string&, const std::string&,
            const std::unordered_map<std::string, std::string>&) {
          return echo::core::HttpResult{500, "", "unexpected POST"};
        });
    return svc.GetUserVip(device, "42", "token");
  };

  // Presence is checked before any .at(), so a missing entitlement_summary
  // reports itself as a contract violation instead of aborting the process
  // through a json out_of_range exception.
  const auto requireSummary = [](const nlohmann::json& out, const char* label)
      -> const nlohmann::json* {
    if (!out.contains("data") || !out["data"].is_object() ||
        !out["data"].contains("entitlement_summary")) {
      std::cerr << "[YouthVipContract] " << label
                << " produced no entitlement_summary: " << out.dump() << std::endl;
      return nullptr;
    }
    return &out["data"]["entitlement_summary"];
  };

  {
    // 09-29 shape: tvip valid, svip lapsed yesterday.
    const auto out = vipWithBusiList(
        R"([{"product_type":"tvip","is_vip":1,"vip_end_time":"2099-12-31 10:00:00"},)"
        R"({"product_type":"svip","is_vip":0,"vip_end_time":"2026-09-29 12:29:09"}])");
    const auto* summary = requireSummary(out, "09-29 tvip-only");
    if (!summary) return 1;
    const auto& s = *summary;
    if (s.value("tier", std::string{}) != "listen" ||
        !s.at("listen").value("active", false) ||
        s.at("listen").value("source", std::string{}) != "tvip" ||
        s.at("listen").value("end", std::string{}) != "2099-12-31 10:00:00" ||
        s.at("premium").value("active", true) ||
        s.at("premium").value("end", std::string{}) != "2026-09-29 12:29:09") {
      std::cerr << "[YouthVipContract] 09-29 shape must be tier=listen with the lapsed "
                   "svip end still reported: " << s.dump() << std::endl;
      return 1;
    }
    // The daily claim grants tvip only, so the top-level is_vip must NOT be
    // corrected to 1. Anything else would silently redefine "has music rights".
    if (out.at("data").value("is_vip", -1) != 0) {
      std::cerr << "[YouthVipContract] tvip must not raise top-level is_vip" << std::endl;
      return 1;
    }
    // The UI's "部分歌曲需豪华 VIP 才能完整播放" small print hangs on
    // kTvipUnlocksPlayback. It has to travel with the summary: a duplicated
    // constant in the frontend would silently go stale the moment A0 proves
    // tvip playback and only the native side is flipped.
    if (!s.contains("listen_unlocks_playback") ||
        s.at("listen_unlocks_playback").get<bool>() !=
            echo::core::kTvipUnlocksPlayback) {
      std::cerr << "[YouthVipContract] entitlement_summary must carry "
                   "listen_unlocks_playback == kTvipUnlocksPlayback ("
                << echo::core::kTvipUnlocksPlayback << "): " << s.dump() << std::endl;
      return 1;
    }
  }

  {
    // 09-24 shape: the upgrade went through, svip active.
    const auto out = vipWithBusiList(
        R"([{"product_type":"tvip","is_vip":1,"vip_end_time":"2099-12-31 10:00:00"},)"
        R"({"product_type":"svip","is_vip":1,"vip_end_time":"2099-12-31 12:00:00"}])");
    const auto* premiumSummary = requireSummary(out, "09-24 svip-active");
    if (!premiumSummary) return 1;
    const auto& s = *premiumSummary;
    if (s.value("tier", std::string{}) != "premium" ||
        !s.at("premium").value("active", false) ||
        s.at("premium").value("source", std::string{}) != "svip") {
      std::cerr << "[YouthVipContract] 09-24 shape must be tier=premium: "
                << s.dump() << std::endl;
      return 1;
    }
    if (out.at("data").value("is_vip", -1) != 1) {
      std::cerr << "[YouthVipContract] active svip must correct top-level is_vip to 1"
                << std::endl;
      return 1;
    }
  }

  {
    // No entitlement at all: tier=none, and the two buckets must still be
    // present so the UI never has to branch on key existence.
    const auto out = vipWithBusiList(R"([])");
    const auto* noneSummary = requireSummary(out, "empty busi_vip");
    if (!noneSummary) return 1;
    const auto& s = *noneSummary;
    if (s.value("tier", std::string{}) != "none" ||
        !s.contains("listen") || !s.contains("premium") ||
        s.at("listen").value("active", true) ||
        s.at("premium").value("active", true)) {
      std::cerr << "[YouthVipContract] empty busi_vip must give tier=none with both "
                   "buckets present: " << s.dump() << std::endl;
      return 1;
    }
  }

  {
    // musicpack counts as premium, matching the existing is_vip whitelist.
    const auto out = vipWithBusiList(
        R"([{"product_type":"musicpack","is_vip":1,"vip_end_time":"2099-06-01 00:00:00"}])");
    const auto* packSummary = requireSummary(out, "musicpack");
    if (!packSummary) return 1;
    if (packSummary->value("tier", std::string{}) != "premium") {
      std::cerr << "[YouthVipContract] musicpack must count as premium" << std::endl;
      return 1;
    }
  }

  {
    // An unknown product_type grants neither bucket. It must not fall into
    // "premium" just because it is not tvip.
    const auto out = vipWithBusiList(
        R"([{"product_type":"readnovel","is_vip":1,"vip_end_time":"2099-01-01 00:00:00"}])");
    const auto* unknownSummary = requireSummary(out, "unrecognised product_type");
    if (!unknownSummary) return 1;
    const auto& s = *unknownSummary;
    if (s.value("tier", std::string{}) != "none" || s.at("listen").value("active", true)) {
      std::cerr << "[YouthVipContract] an unrecognised product_type granted entitlement: "
                << s.dump() << std::endl;
      return 1;
    }
  }

  std::cout << "[YouthVipContract] entitlement tier contract passed" << std::endl;

  std::cout << "[YouthVipContract] empty 51002 fallback contract passed" << std::endl;
  std::cout << "[YouthVipContract] passed" << std::endl;
  return 0;
}

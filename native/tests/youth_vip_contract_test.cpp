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
    captureGet("vip_query_default", echo::core::KuGouEdition::Standard);
    captureGet("vip_query_concept", echo::core::KuGouEdition::Concept);
  }

  const auto result = service.ClaimYouthListenSong(device, "42", "token");

  // Production claim contract (ADR-0006): youth reward endpoints use the
  // STANDARD Android identity from the reference (1005/20489/standard salt),
  // with listen overriding clientver to 10566. Concept (kProjectEdition) is
  // an unfinished candidate for claims — not production, not this assertion.
  const auto productionClaim = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Standard);
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
    std::cerr << "[YouthVipContract] listen request must use production Standard claim "
              << "identity (appid=" << productionClaim.appid << " + standard salt, "
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
    std::cerr << "[YouthVipContract] GetUserVip() default must remain Standard "
              << "(UserService API default; production VIP *query* route may "
              << "explicitly inject Concept per UserRoutes evidence — that is "
              << "a different call site)" << std::endl;
    return 1;
  }

  // ── Production claim contract: receive_vip_listen_song uses Standard ────
  // Claims stay on the reference-aligned Standard Android identity (1005/20489/
  // standard salt). VIP/playlist *query* production routes inject Concept after
  // 2026-09-15 evidence; that evidence must NOT be extended to claim routes.
  // Params stay in the query with an empty body; fingerprint headers stay as
  // they are. uuid/headers/device are not part of this identity family.
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
    std::cerr << "[YouthVipContract] receive_vip_listen_song must use production Standard "
              << "identity (appid=" << productionClaim.appid
              << " clientver=" << productionClaim.clientver
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

  std::cout << "[YouthVipContract] day-vip production Standard claim contract passed"
            << std::endl;

  // Production claims and production VIP query are intentionally different
  // families (Standard claim vs Concept query route). Do not require their
  // appid/clientver to match — query A/B evidence does not close claim 51002.
  if (QueryValue(dayUrl, "appid") == productionClaim.appid &&
      QueryValue(dayUrl, "clientver") == productionClaim.clientver) {
    std::cout << "[YouthVipContract] claim identity stays Standard; VIP query route "
              << "Concept injection is a separate production contract" << std::endl;
  }

  // ── Day claim Concept CANDIDATE (explicit injection, offline only) ──────
  // Production default remains Standard (asserted above). This block builds
  // the REAL ClaimVip request path with Concept profile and verifies the full
  // request contract + signature recompute from final query params + body.
  // Other claim routes are untouched. NOT a 51002 root-cause claim; NOT
  // production default; NOT evidence that Concept claims succeed upstream.
  const auto conceptClaim = echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
  std::string candUrl;
  std::string candBody = "UNSET";
  std::unordered_map<std::string, std::string> candHeaders;
  echo::core::UserService candService(
      [&](const std::string&,
          const std::unordered_map<std::string, std::string>&) {
        return echo::core::HttpResult{500, "", "unexpected GET"};
      },
      [&](const std::string& url, const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        candUrl = url;
        candBody = body;
        candHeaders = headers;
        return echo::core::HttpResult{
            200, R"({"status":0,"error_code":51002,"error_msg":"device validation failed"})", ""};
      });
  candService.ClaimVip(device, "42", "token", echo::core::KuGouEdition::Concept);

  const auto candClienttime = QueryValue(candUrl, "clienttime");
  const auto candReceiveDay = QueryValue(candUrl, "receive_day");
  const std::unordered_map<std::string, std::string> candSignedParams = {
      {"appid", QueryValue(candUrl, "appid")},
      {"clientver", QueryValue(candUrl, "clientver")},
      {"clienttime", candClienttime},
      {"dfid", QueryValue(candUrl, "dfid")},
      {"mid", QueryValue(candUrl, "mid")},
      {"uuid", QueryValue(candUrl, "uuid")},
      {"userid", QueryValue(candUrl, "userid")},
      {"token", QueryValue(candUrl, "token")},
      {"source_id", QueryValue(candUrl, "source_id")},
      {"receive_day", candReceiveDay},
  };
  const auto candSignature = QueryValue(candUrl, "signature");
  const auto candRecomputed = echo::core::SignatureAndroidParams(
      candSignedParams, /*body=*/"", conceptClaim.saltKind);
  // Fixed non-profile params must match production day claim — only the
  // whole profile (appid/clientver/salt → signature) may differ.
  const bool candFixedParamsMatchProduction =
      QueryValue(candUrl, "dfid") == device.dfid &&
      QueryValue(candUrl, "mid") == device.mid &&
      QueryValue(candUrl, "uuid") == "-" &&
      QueryValue(candUrl, "userid") == "42" &&
      QueryValue(candUrl, "token") == "token" &&
      QueryValue(candUrl, "source_id") == "90139" &&
      QueryValue(candUrl, "plat").empty() &&
      candReceiveDay.size() == 10 && candReceiveDay[4] == '-' && candReceiveDay[7] == '-' &&
      candBody.empty() &&
      candHeaders["Content-Type"] == "application/x-www-form-urlencoded" &&
      candHeaders["clienttime"] == candClienttime &&
      candHeaders["dfid"] == device.dfid &&
      candHeaders["mid"] == device.mid &&
      candHeaders["kg-rc"] == "1" &&
      candHeaders["kg-thash"] == "5d816a0" &&
      candHeaders["kg-rec"] == "1" &&
      candHeaders["kg-rf"] == "B9EDA08A64250DEFFBCADDEE00F8F25F" &&
      candHeaders["User-Agent"] ==
          "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";
  const bool candProfileIsConcept =
      candUrl.find("https://gateway.kugou.com/youth/v1/recharge/receive_vip_listen_song?") == 0 &&
      QueryValue(candUrl, "appid") == conceptClaim.appid &&
      QueryValue(candUrl, "clientver") == conceptClaim.clientver &&
      !candSignature.empty() &&
      candSignature == candRecomputed &&
      // Must actually differ from the production Standard request family.
      QueryValue(candUrl, "appid") != productionClaim.appid &&
      QueryValue(candUrl, "clientver") != productionClaim.clientver &&
      QueryValue(candUrl, "signature") != QueryValue(dayUrl, "signature");
  if (!candFixedParamsMatchProduction || !candProfileIsConcept) {
    std::cerr << "[YouthVipContract] day Concept candidate must keep fixed params and "
              << "switch only the whole profile; got appid=" << QueryValue(candUrl, "appid")
              << " clientver=" << QueryValue(candUrl, "clientver")
              << " uuid=" << QueryValue(candUrl, "uuid")
              << " signature_match=" << (candSignature == candRecomputed ? "Y" : "N")
              << " fixed_ok=" << (candFixedParamsMatchProduction ? "Y" : "N") << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] day Concept CANDIDATE offline request contract passed "
            << "(appid=" << conceptClaim.appid
            << " clientver=" << conceptClaim.clientver
            << " salt=" << echo::core::KuGouSaltKindName(conceptClaim.saltKind)
            << "; production default remains Standard; not a claim-success claim)"
            << std::endl;

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
    std::cerr << "[YouthVipContract] upgrade_vip_reward must use production Standard identity; "
              << "got appid=" << QueryValue(upgradeUrl, "appid")
              << " clientver=" << QueryValue(upgradeUrl, "clientver") << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] upgrade production Standard claim contract passed"
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
  // Production ad contract (current code, not yet the reference): Standard
  // signing identity; uuid falls through to device.guid (ad does not force
  // uuid="-"); headers today omit public device fingerprint keys. Those two
  // diffs vs the local reference are investigation notes — not claimed 51002
  // root causes, and not switched here.
  const bool adMatchesProductionIdentity =
      adUrl.find("https://gateway.kugou.com/youth/v1/ad/play_report?") == 0 &&
      QueryValue(adUrl, "appid") == productionClaim.appid &&
      QueryValue(adUrl, "clientver") == productionClaim.clientver &&
      QueryValue(adUrl, "uuid") == device.guid &&
      QueryValue(adUrl, "signature") ==
          echo::core::SignatureAndroidParams(adSignedParams, adBody, productionClaim.saltKind) &&
      adBody.find("\"ad_id\":12307537187") != std::string::npos &&
      adHeaders.count("dfid") == 0 &&
      adHeaders.count("kg-rf") == 0;
  if (!adMatchesProductionIdentity) {
    std::cerr << "[YouthVipContract] ad play_report must use production Standard identity; "
              << "got appid=" << QueryValue(adUrl, "appid")
              << " clientver=" << QueryValue(adUrl, "clientver")
              << " uuid=" << QueryValue(adUrl, "uuid") << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] ad production Standard claim identity contract passed"
            << std::endl;

  // ── Candidate profile TABLE pin only (not a claim request) ──────────────
  // Concept day-claim request construction is asserted above via the real
  // ClaimVip(edition) path. This block only pins the profile table constants
  // so a table drift cannot silently change the candidate identity.
  const auto conceptClaimCandidate =
      echo::core::GetKuGouProfile(echo::core::KuGouEdition::Concept);
  if (conceptClaimCandidate.appid != "3116" ||
      conceptClaimCandidate.clientver != "11440" ||
      conceptClaimCandidate.saltKind != echo::core::KuGouSaltKind::Lite) {
    std::cerr << "[YouthVipContract] Concept candidate profile table drifted: appid="
              << conceptClaimCandidate.appid
              << " clientver=" << conceptClaimCandidate.clientver
              << " salt=" << echo::core::KuGouSaltKindName(conceptClaimCandidate.saltKind)
              << std::endl;
    return 1;
  }
  std::cout << "[YouthVipContract] Concept profile table pin appid="
            << conceptClaimCandidate.appid
            << " clientver=" << conceptClaimCandidate.clientver
            << " salt=" << echo::core::KuGouSaltKindName(conceptClaimCandidate.saltKind)
            << " (production day claim stays Standard)" << std::endl;

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

  std::cout << "[YouthVipContract] empty 51002 fallback contract passed" << std::endl;
  std::cout << "[YouthVipContract] passed" << std::endl;
  return 0;
}

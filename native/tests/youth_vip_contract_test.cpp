#include <iostream>
#include <string>
#include <unordered_map>

#include "echo/core/Crypto.h"
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

  const auto result = service.ClaimYouthListenSong(device, "42", "token");

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
      QueryValue(capturedUrl, "appid") == "1005" &&
      QueryValue(capturedUrl, "clientver") == "10566" &&
      QueryValue(capturedUrl, "dfid") == device.dfid &&
      QueryValue(capturedUrl, "mid") == device.mid &&
      QueryValue(capturedUrl, "uuid") == "-" &&
      QueryValue(capturedUrl, "userid") == "42" &&
      QueryValue(capturedUrl, "token") == "token" &&
      QueryValue(capturedUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              signedParams, capturedBody, echo::core::KuGouSaltKind::Standard) &&
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
    std::cerr << "[YouthVipContract] request does not match the registered Android contract"
              << std::endl;
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
      QueryValue(vipUrl, "appid") == "3116" &&
      QueryValue(vipUrl, "clientver") == "11440" &&
      QueryValue(vipUrl, "busi_type") == "concept" &&
      QueryValue(vipUrl, "uuid") == "-" &&
      QueryValue(vipUrl, "product_type").empty() &&
      QueryValue(vipUrl, "opt_product_types").empty() &&
      QueryValue(vipUrl, "signature") ==
          echo::core::SignatureAndroidParams(
              vipSignedParams, "", echo::core::KuGouSaltKind::Lite) &&
      !vipClienttime.empty() && cookieHasSessionKeys;

  if (!vipRequestMatchesReference) {
    std::cerr << "[YouthVipContract] get_union_vip request does not match the Concept contract"
              << std::endl;
    return 1;
  }

  std::cout << "[YouthVipContract] passed" << std::endl;
  return 0;
}

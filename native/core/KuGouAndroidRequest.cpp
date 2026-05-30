#include "echo/core/KuGouAndroidRequest.h"

#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace echo::core {

std::string BuildSignedUrl(const KuGouAndroidRequest& req) {
  std::unordered_map<std::string, std::string> params;
  
  std::string clienttime = req.params.count("clienttime") ? req.params.at("clienttime") : std::to_string(std::time(nullptr));
  std::string dfid = req.device.dfid.empty() ? "-" : req.device.dfid;
  std::string mid = ResolveAndroidMid(req.device);
  std::string uuid = req.device.guid.empty() ? "-" : req.device.guid;

  params["appid"] = req.profile.appid;
  params["clientver"] = req.profile.clientver;
  params["clienttime"] = clienttime;
  params["dfid"] = dfid;
  params["mid"] = mid;
  params["uuid"] = uuid;

  for (const auto& [k, v] : req.params) {
    params[k] = v;
  }

  params["signature"] = SignatureAndroidParams(params, req.body, req.profile.saltKind);

  if (req.includeSongUrlKey) {
    std::string hash = params.count("hash") ? params["hash"] : "";
    std::string userid = params.count("userid") ? params["userid"] : "0";
    if (!hash.empty()) {
      params["key"] = SignKey(hash, mid, userid, req.profile.appid, req.profile.saltKind);
    }
  }

  std::ostringstream urlStream;
  urlStream << req.endpoint << "?";
  bool first = true;
  for (const auto& [k, v] : params) {
    if (!first) urlStream << "&";
    urlStream << k << "=" << v;
    first = false;
  }
  return urlStream.str();
}

std::unordered_map<std::string, std::string> BuildAndroidHeaders(const KuGouAndroidRequest& req) {
  std::unordered_map<std::string, std::string> headers;
  std::string clienttime = req.params.count("clienttime") ? req.params.at("clienttime") : std::to_string(std::time(nullptr));
  std::string dfid = req.device.dfid.empty() ? "-" : req.device.dfid;
  std::string mid = ResolveAndroidMid(req.device);

  headers["kg-rf"] = "B9EDA08A64250DEFFBCADDEE00F8F25F";
  headers["Accept"] = "application/json";
  headers["clienttime"] = clienttime;
  headers["dfid"] = dfid;
  headers["mid"] = mid;
  return headers;
}

}  // namespace echo::core

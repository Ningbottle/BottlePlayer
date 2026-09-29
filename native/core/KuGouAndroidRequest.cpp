#include "echo/core/KuGouAndroidRequest.h"

#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/StringUtils.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace echo::core {

std::string BuildSignedUrl(const KuGouAndroidRequest& req) {
  std::unordered_map<std::string, std::string> params;

  // Always inject appid and clientver
  params["appid"] = req.profile.appid;
  params["clientver"] = req.profile.clientver;

  // Compute device values (needed for includeSongUrlKey even if skipDeviceDefaults)
  std::string dfid = req.device.dfid.empty() ? "-" : req.device.dfid;
  std::string mid = ResolveAndroidMid(req.device);
  std::string uuid = req.device.guid.empty() ? "-" : req.device.guid;

  // Inject device defaults unless skipDeviceDefaults is set
  if (!req.skipDeviceDefaults) {
    std::string clienttime = req.params.count("clienttime") ? req.params.at("clienttime") : std::to_string(std::time(nullptr));

    params["clienttime"] = clienttime;
    params["dfid"] = dfid;
    params["mid"] = mid;
    params["uuid"] = uuid;
  }

  for (const auto& [k, v] : req.params) {
    params[k] = v;
  }

  if (req.includeSongUrlKey) {
    std::string hash = params.count("hash") ? params["hash"] : "";
    std::string userid = params.count("userid") ? params["userid"] : "0";
    // key 必须使用最终 query 里的 mid，而不是最初 device 推导出的 mid。
    // 匿名回退会显式写入 mid=0；这里跟随最终参数，避免 URL 和签名身份不一致。
    std::string signedMid = params.count("mid") ? params["mid"] : "0";
    if (!hash.empty()) {
      params["key"] = SignKey(hash, signedMid, userid, req.profile.appid, req.profile.saltKind);
    }
  }

  // reference notSign:true（如 song_url.js）：不附加 signature，但 key 仍保留。
  if (!req.notSign) {
    params["signature"] = SignatureAndroidParams(params, req.body, req.profile.saltKind);
  }

  std::ostringstream urlStream;
  urlStream << req.endpoint << "?";
  bool first = true;
  for (const auto& [k, v] : params) {
    if (!first) urlStream << "&";
    urlStream << k << "=" << UrlEncode(v);
    first = false;
  }
  return urlStream.str();
}

std::unordered_map<std::string, std::string> BuildAndroidHeaders(const KuGouAndroidRequest& req) {
  std::unordered_map<std::string, std::string> headers;
  // Device-derived defaults (mirrors BuildSignedUrl). req.params may override
  // these (e.g. for explicit-identity tests where mid="0"/dfid="-"), so that
  // the URL query string and the HTTP headers stay consistent.
  //
  // skipDeviceDefaults mirrors BuildSignedUrl's gate: when set, no device
  // defaults are emitted unless the caller supplied an explicit override in
  // req.params. This keeps the URL query string and the HTTP headers in lock-
  // step (a caller that omits dfid/mid from the URL must not send device-
  // derived values in the headers, or the signed request diverges).
  std::string clienttime;
  std::string dfid;
  std::string mid;
  if (req.skipDeviceDefaults) {
    // Only emit what the caller explicitly provided; fall back to "-" / "0"
    // only if a header value is required but was not supplied.
    if (req.params.count("clienttime")) clienttime = req.params.at("clienttime");
    if (req.params.count("dfid")) dfid = req.params.at("dfid");
    if (req.params.count("mid")) mid = req.params.at("mid");
  } else {
    clienttime = req.params.count("clienttime") ? req.params.at("clienttime") : std::to_string(std::time(nullptr));
    dfid = req.params.count("dfid") ? req.params.at("dfid")
                                    : (req.device.dfid.empty() ? "-" : req.device.dfid);
    mid = req.params.count("mid") ? req.params.at("mid") : ResolveAndroidMid(req.device);
  }

  headers["kg-rf"] = "B9EDA08A64250DEFFBCADDEE00F8F25F";
  // KuGou anti-crawl fingerprint headers — fixed constants emitted on every
  // request, matching the reference implementation
  // (MakcRe/KuGouMusicApi util/request.js:41).
  headers["kg-rc"] = "1";
  headers["kg-thash"] = "5d816a0";
  headers["kg-rec"] = "1";
  headers["Accept"] = "application/json";
  // Only emit the identity headers when we have a value. In skipDeviceDefaults
  // mode with no explicit override, dfid/mid/clienttime are left empty above and
  // are omitted here — mirroring BuildSignedUrl, which also injects them only
  // when !skipDeviceDefaults (or when the caller supplied them via req.params).
  if (!clienttime.empty()) headers["clienttime"] = clienttime;
  if (!dfid.empty()) headers["dfid"] = dfid;
  if (!mid.empty()) headers["mid"] = mid;
  return headers;
}

}  // namespace echo::core

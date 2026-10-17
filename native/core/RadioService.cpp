#include "echo/core/RadioService.h"
#include "echo/core/Crypto.h"
#include "echo/core/DeviceService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include <algorithm>
#include <chrono>
#include <utility>

namespace echo::core {
namespace {
nlohmann::json Error(const std::string& message, long status = 0) {
  return {{"status", 0}, {"error", message}, {"status_code", status}, {"data", nlohmann::json::array()}};
}
}
RadioService::RadioService() : RadioService([](const auto& url, const auto& body, const auto& headers) {
  HttpClient client;
  return client.Post(url, body, headers);
}) {}
RadioService::RadioService(RadioHttpPost httpPost) : httpPost_(std::move(httpPost)) {}

nlohmann::json RadioService::Recommend(const RadioContext& context) const {
  // Only the observed play contract is supported. Do not invent "like/dislike" actions.
  if (context.action != "play") return Error("unsupported_radio_action");
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto clientTime = std::to_string(nowMs);
  nlohmann::json data = {
      {"appid", profile.appid}, {"clienttime", nowMs}, {"mid", ResolveAndroidMid(context.device)},
      {"action", "play"}, {"recommend_source_locked", 0}, {"song_pool_id", std::max(0, context.songPoolId)},
      {"callerid", 0}, {"m_type", 1}, {"platform", "ios"}, {"area_code", 1},
      {"remain_songcnt", std::clamp(context.remainingSongs, 0, 100)}, {"clientver", profile.clientver},
      {"is_overplay", context.overplay ? 1 : 0}, {"mode", "normal"},
      {"fakem", "ca981cfc583a4c37f28d2d49000013c16a0a"},
      {"key", SignParamsKey(clientTime, profile.appid, profile.clientver, profile.saltKind)},
  };
  if (!context.userId.empty() && context.userId != "0") {
    data["userid"] = context.userId; data["kguid"] = context.userId;
  }
  if (!context.token.empty()) data["token"] = context.token;
  if (!context.hash.empty()) data["hash"] = context.hash;
  if (!context.songId.empty()) data["songid"] = context.songId;
  if (!context.hash.empty() && context.playedSeconds > 0) data["playtime"] = std::min(context.playedSeconds, 86'400);

  KuGouAndroidRequest request;
  request.endpoint = "https://gateway.kugou.com/v2/personal_recommend";
  request.profile = profile; request.device = context.device; request.body = data.dump();
  request.params["clienttime"] = clientTime;
  const auto url = BuildSignedUrl(request);
  auto headers = BuildAndroidHeaders(request);
  headers["Content-Type"] = "application/json";
  headers["User-Agent"] = "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi";
  headers["x-router"] = "persnfm.service.kugou.com";
  const auto result = httpPost_(url, request.body, headers);
  if (!result.error.empty()) return Error(result.error, result.statusCode);
  try { return nlohmann::json::parse(result.body); }
  catch (const nlohmann::json::exception&) { return Error("invalid_radio_response", result.statusCode); }
}
}  // namespace echo::core

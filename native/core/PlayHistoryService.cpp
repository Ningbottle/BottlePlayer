#include "echo/core/PlayHistoryService.h"
#include "echo/core/KuGouAndroidRequest.h"
#include "echo/core/KuGouProfile.h"
#include "echo/core/SafeStoll.h"

#include <ctime>

namespace echo::core {
namespace {

nlohmann::json MakeError(const std::string& message, long statusCode = 0) {
  return {
      {"status", 0},
      {"error", message},
      {"status_code", statusCode},
  };
}

}  // namespace

PlayHistoryService::PlayHistoryService()
    : PlayHistoryService([](const std::string& url,
                            const std::string& body,
                            const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Post(url, body, headers);
      }) {}

PlayHistoryService::PlayHistoryService(PlayHistoryHttpPost httpPost)
    : httpPost_(std::move(httpPost)) {}

nlohmann::json PlayHistoryService::UploadSong(
    const std::string& userId,
    const std::string& token,
    long long mxid,
    long long time,
    int pc) const {
  if (time == 0) {
    time = std::time(nullptr);
  }

  nlohmann::json song = {
      {"mxid", mxid},
      {"op", 1},
      {"ot", time},
      {"pc", pc}
  };
  nlohmann::json songsArray = nlohmann::json::array({song});
  nlohmann::json dataMap = {
      {"songs", songsArray},
      {"token", token},
      {"userid", SafeStoll(userId)}
  };
  const std::string body = dataMap.dump();

  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/playhistory/v1/upload_songs";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);
  req.body = body;
  req.params["plat"] = "3";
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json PlayHistoryService::GetUserHistory(
    const std::string& userId,
    const std::string& token,
    const std::string& bp) const {
  nlohmann::json dataMap = {
      {"token", token},
      {"userid", SafeStoll(userId)},
      {"source_classify", "app"},
      {"to_subdivide_sr", 1}
  };
  if (!bp.empty()) {
    dataMap["bp"] = bp;
  }
  const std::string body = dataMap.dump();

  KuGouAndroidRequest req;
  req.endpoint = "https://gateway.kugou.com/playhistory/v1/get_songs";
  req.profile = GetKuGouProfile(KuGouEdition::Concept);
  req.body = body;
  if (!userId.empty() && userId != "0") req.params["userid"] = userId;
  if (!token.empty()) req.params["token"] = token;

  const std::string url = BuildSignedUrl(req);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

}  // namespace echo::core

#include "echo/core/PlayHistoryService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"

#include <ctime>
#include <sstream>
#include <iomanip>

namespace echo::core {
namespace {

std::string UrlEncode(std::string_view value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      stream << static_cast<char>(ch);
    } else {
      stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return stream.str();
}

std::string BuildAndroidSignedUrl(
    const std::string& baseUrl,
    std::unordered_map<std::string, std::string> params,
    const std::string& body = "") {
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  if (params.find("appid") == params.end()) params["appid"] = profile.appid;
  if (params.find("clientver") == params.end()) params["clientver"] = profile.clientver;
  if (params.find("clienttime") == params.end()) {
    params["clienttime"] = std::to_string(std::time(nullptr));
  }
  if (params.find("mid") == params.end()) params["mid"] = "0";
  if (params.find("uuid") == params.end()) params["uuid"] = "-";
  if (params.find("dfid") == params.end()) params["dfid"] = "-";

  params["signature"] = SignatureAndroidParams(params, body, profile.saltKind);

  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(value);
    first = false;
  }
  return urlStream.str();
}

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
      {"userid", userId.empty() ? 0 : std::stoll(userId)}
  };
  const std::string body = dataMap.dump();

  std::unordered_map<std::string, std::string> params;
  params["plat"] = "3";
  if (!userId.empty() && userId != "0") params["userid"] = userId;
  if (!token.empty()) params["token"] = token;

  std::string url = BuildAndroidSignedUrl("https://gateway.kugou.com/playhistory/v1/upload_songs", params, body);

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
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"source_classify", "app"},
      {"to_subdivide_sr", 1}
  };
  if (!bp.empty()) {
    dataMap["bp"] = bp;
  }
  const std::string body = dataMap.dump();

  std::unordered_map<std::string, std::string> params;
  if (!userId.empty() && userId != "0") params["userid"] = userId;
  if (!token.empty()) params["token"] = token;

  std::string url = BuildAndroidSignedUrl("https://gateway.kugou.com/playhistory/v1/get_songs", params, body);

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

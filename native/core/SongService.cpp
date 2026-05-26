#include "echo/core/SongService.h"
#include "echo/core/Crypto.h"

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
  if (params.find("appid") == params.end()) params["appid"] = "3116";
  if (params.find("clientver") == params.end()) params["clientver"] = "11440";
  if (params.find("clienttime") == params.end()) {
    params["clienttime"] = std::to_string(std::time(nullptr));
  }
  if (params.find("mid") == params.end()) params["mid"] = "0";
  if (params.find("uuid") == params.end()) params["uuid"] = "-";
  if (params.find("dfid") == params.end()) params["dfid"] = "-";

  params["signature"] = SignatureAndroidParams(params, body);

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
      {"data", nlohmann::json::array()},
  };
}

}  // namespace

SongService::SongService()
    : SongService([](const std::string& url,
                     const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

SongService::SongService(SongHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json SongService::GetClimax(const std::string& hash) const {
  nlohmann::json data = nlohmann::json::array();
  std::stringstream ss(hash);
  std::string s;
  while (std::getline(ss, s, ',')) {
    if (!s.empty()) {
      data.push_back({{"hash", s}});
    }
  }

  std::unordered_map<std::string, std::string> params;
  params["data"] = data.dump();

  std::string url = BuildAndroidSignedUrl("https://expendablekmrcdn.kugou.com/v1/audio_climax/audio", params);
  const auto result = httpGet_(url, {
    {"Accept", "application/json"},
    {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
  });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json SongService::GetRanking(const std::string& albumAudioId) const {
  std::unordered_map<std::string, std::string> params;
  params["album_audio_id"] = albumAudioId;

  std::string url = BuildAndroidSignedUrl("https://gateway.kugou.com/grow/v1/song_ranking/play_page/ranking_info", params);
  const auto result = httpGet_(url, {
    {"Accept", "application/json"},
    {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"}
  });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json SongService::GetRankingFilter(const std::string& albumAudioId, int page, int pageSize) const {
  std::unordered_map<std::string, std::string> params;
  params["album_audio_id"] = albumAudioId;
  params["page"] = std::to_string(page);
  params["pagesize"] = std::to_string(pageSize);

  std::string url = BuildAndroidSignedUrl("https://gateway.kugou.com/grow/v1/song_ranking/unlock/v2/ranking_filter", params);
  const auto result = httpGet_(url, {
    {"Accept", "application/json"},
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

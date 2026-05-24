#include "echo/core/HomeService.h"
#include "echo/core/Crypto.h"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>

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

std::string BuildSignedUrl(
    const std::string& baseUrl,
    std::unordered_map<std::string, std::string> params,
    const std::string& body = "") {
  params["signature"] = SignatureAndroidParams(params, body);
  std::ostringstream urlStream;
  urlStream << baseUrl << "?";
  bool first = true;
  for (const auto& [key, value] : params) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << value;
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

std::unordered_map<std::string, std::string> BaseParams(
    const std::string& userId,
    const std::string& token) {
  const auto clienttime = std::to_string(std::time(nullptr));
  std::unordered_map<std::string, std::string> params = {
      {"appid", "1014"},
      {"clientver", "20000"},
      {"clienttime", clienttime},
      {"plat", "1"},
  };
  if (!userId.empty() && userId != "0") params["userid"] = userId;
  if (!token.empty()) params["token"] = token;
  return params;
}

}  // namespace

HomeService::HomeService()
    : HomeService(
          [](const std::string& url,
             const std::string& body,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Post(url, body, headers);
          },
          [](const std::string& url,
             const std::unordered_map<std::string, std::string>& headers) {
            HttpClient client;
            return client.Get(url, headers);
          }) {}

HomeService::HomeService(HomeHttpPost httpPost)
    : httpPost_(std::move(httpPost)),
      httpGet_([](const std::string& url,
                  const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

HomeService::HomeService(HomeHttpPost httpPost, HomeHttpGet httpGet)
    : httpPost_(std::move(httpPost)), httpGet_(std::move(httpGet)) {}

nlohmann::json HomeService::GetBanners(
    const std::string& userId,
    const std::string& token) const {
  nlohmann::json dataPayload = {
      {"plat", 0},
      {"channel", 201},
      {"operator", 7},
      {"networktype", 2},
      {"userid", userId.empty() ? 0 : std::stoll(userId)},
      {"vip_type", 0},
      {"m_type", 0},
      {"tags", nlohmann::json::array()},
      {"apiver", 5},
      {"ability", 2},
      {"mode", "normal"},
  };
  const std::string body = dataPayload.dump();

  auto params = BaseParams(userId, token);
  const std::string url =
      BuildSignedUrl("https://gateway.kugou.com/ads.gateway/v3/listen_banner", params, body);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json HomeService::GetEverydayRecommend(
    const std::string& userId,
    const std::string& token) const {
  // Empty body — the endpoint uses query params only.
  const std::string body;

  auto params = BaseParams(userId, token);
  params["platform"] = "ios";
  const std::string url =
      BuildSignedUrl("https://gateway.kugou.com/everyday_song_recommend", params, body);

  const auto result = httpPost_(
      url,
      body,
      {
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"User-Agent", "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"x-router", "everydayrec.service.kugou.com"},
      });

  if (!result.error.empty()) return MakeError(result.error, result.statusCode);

  try {
    return nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& e) {
    return MakeError(std::string("JSON parse error: ") + e.what());
  }
}

nlohmann::json HomeService::GetImagesAudio(const std::string& hash,
                                           const std::string& audioId,
                                           const std::string& albumAudioId,
                                           const std::string& filename,
                                           int count) const {
  std::vector<std::string> hashes;
  {
    std::stringstream ss(hash);
    std::string s;
    while (std::getline(ss, s, ',')) {
      hashes.push_back(s);
    }
  }

  nlohmann::json data = nlohmann::json::array();
  for (const auto& h : hashes) {
    if (!h.empty()) {
      data.push_back({
          {"audio_id", 0},
          {"hash", h},
          {"album_audio_id", 0},
          {"filename", ""}
      });
    }
  }

  if (!audioId.empty()) {
    std::stringstream ss(audioId);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      try {
        data[index]["audio_id"] = s.empty() ? 0 : std::stoll(s);
      } catch (...) {
        data[index]["audio_id"] = 0;
      }
      index++;
    }
  }

  if (!albumAudioId.empty()) {
    std::stringstream ss(albumAudioId);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      try {
        data[index]["album_audio_id"] = s.empty() ? 0 : std::stoll(s);
      } catch (...) {
        data[index]["album_audio_id"] = 0;
      }
      index++;
    }
  }

  if (!filename.empty()) {
    std::stringstream ss(filename);
    std::string s;
    size_t index = 0;
    while (std::getline(ss, s, ',') && index < data.size()) {
      data[index]["filename"] = s;
      index++;
    }
  }

  std::unordered_map<std::string, std::string> paramsMap;
  paramsMap["appid"] = "1005";
  paramsMap["clientver"] = "20489";
  paramsMap["count"] = std::to_string(count);
  paramsMap["data"] = data.dump();
  paramsMap["isCdn"] = "1";
  paramsMap["publish_time"] = "1";
  paramsMap["show_authors"] = "1";

  const std::string signature = SignatureAndroidParams(paramsMap);

  std::vector<std::string> keys;
  keys.reserve(paramsMap.size());
  for (const auto& [k, _] : paramsMap) keys.push_back(k);
  std::sort(keys.begin(), keys.end());

  std::ostringstream urlStream;
  urlStream << "https://expendablekmr.kugou.com/v2/author_image/audio?";
  bool first = true;
  for (const auto& key : keys) {
    if (!first) urlStream << "&";
    urlStream << key << "=" << UrlEncode(paramsMap[key]);
    first = false;
  }
  urlStream << "&signature=" << signature;
  std::string url = urlStream.str();

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

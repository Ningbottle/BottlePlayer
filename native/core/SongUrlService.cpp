#include "echo/core/SongUrlService.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace echo::core {
namespace {

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  if (first > 0) value.erase(0, first);
  return value;
}

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

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  return "";
}

nlohmann::json EmptySongUrl(std::string hash, std::string quality, std::string error) {
  return {
      {"status", 0},
      {"error_code", error.empty() ? "native_song_url_empty" : "native_song_url_failed"},
      {"error", std::move(error)},
      {"url", ""},
      {"play_url", ""},
      {"playUrl", ""},
      {"data",
       {
           {"hash", std::move(hash)},
           {"quality", std::move(quality)},
           {"url", ""},
           {"play_url", ""},
           {"playUrl", ""},
           {"backup_url", nlohmann::json::array()},
       }},
  };
}

nlohmann::json NormalizeBackupUrl(const nlohmann::json& value) {
  if (value.is_array()) return value;
  if (value.is_string() && !value.get<std::string>().empty()) {
    return nlohmann::json::array({value.get<std::string>()});
  }
  if (value.is_object()) {
    nlohmann::json urls = nlohmann::json::array();
    for (const auto& item : value.items()) {
      if (item.value().is_string() && !item.value().get<std::string>().empty()) {
        urls.push_back(item.value().get<std::string>());
      } else if (item.value().is_array()) {
        for (const auto& nested : item.value()) {
          if (nested.is_string() && !nested.get<std::string>().empty()) {
            urls.push_back(nested.get<std::string>());
          }
        }
      }
    }
    return urls;
  }
  return nlohmann::json::array();
}

}  // namespace

SongUrlService::SongUrlService()
    : SongUrlService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

SongUrlService::SongUrlService(SongUrlHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json SongUrlService::Resolve(
    std::string hash,
    std::string quality,
    std::string ppageId) const {
  hash = Trim(std::move(hash));
  quality = Trim(std::move(quality));
  ppageId = Trim(std::move(ppageId));

  if (hash.empty()) {
    return EmptySongUrl(hash, quality, "Missing song hash");
  }

  std::string url = "http://m.kugou.com/app/i/getSongInfo.php?cmd=playInfo&hash=" + UrlEncode(hash);
  if (!ppageId.empty()) {
    url += "&ppage_id=" + UrlEncode(ppageId);
  }

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) {
    return EmptySongUrl(hash, quality, result.error);
  }

  if (result.statusCode < 200 || result.statusCode >= 300) {
    return EmptySongUrl(hash, quality, "Kugou song URL upstream returned an error");
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return EmptySongUrl(hash, quality, std::string("Invalid Kugou song URL JSON: ") + error.what());
  }

  const auto playUrl = ReadString(upstream, "url");
  const auto upstreamHash = ReadString(upstream, "hash");
  const auto backupUrl = NormalizeBackupUrl(upstream.value("backup_url", nlohmann::json::array()));
  const bool ok = !playUrl.empty();
  const auto error = upstream.value("error", "");

  return {
      {"status", ok ? 1 : 0},
      {"error", error},
      {"error_code", ok ? "" : "native_song_url_empty"},
      {"url", playUrl},
      {"play_url", playUrl},
      {"playUrl", playUrl},
      {"data",
       {
           {"hash", upstreamHash.empty() ? hash : upstreamHash},
           {"req_hash", upstream.value("req_hash", hash)},
           {"quality", quality},
           {"url", playUrl},
           {"play_url", playUrl},
           {"playUrl", playUrl},
           {"backup_url", backupUrl},
           {"fileName", upstream.value("fileName", "")},
           {"songName", upstream.value("songName", "")},
           {"singerName", upstream.value("singerName", "")},
           {"albumid", upstream.value("albumid", 0)},
           {"album_audio_id", upstream.value("album_audio_id", 0)},
           {"audio_id", upstream.value("audio_id", 0)},
           {"timeLength", upstream.value("timeLength", 0)},
           {"bitRate", upstream.value("bitRate", 0)},
           {"extName", upstream.value("extName", "")},
           {"privilege", upstream.value("privilege", 0)},
           {"pay_type", upstream.value("pay_type", 0)},
           {"raw", upstream},
       }},
  };
}

}  // namespace echo::core

#include "echo/core/PrivilegeService.h"

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

int ReadInt(const nlohmann::json& value, std::string_view key, int fallback = 0) {
  if (!value.contains(key)) return fallback;
  const auto& item = value.at(key);
  if (item.is_number_integer()) return item.get<int>();
  if (item.is_number_unsigned()) return static_cast<int>(item.get<unsigned int>());
  if (item.is_string()) {
    try {
      return std::stoi(item.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

void AddRelateGood(
    nlohmann::json& goods,
    const nlohmann::json& source,
    std::string_view hashKey,
    std::string quality,
    int level) {
  const auto hash = ReadString(source, hashKey);
  if (hash.empty()) return;
  goods.push_back({{"hash", hash}, {"quality", std::move(quality)}, {"level", level}});
}

nlohmann::json EmptyPrivilege(std::string hash, std::string albumId, std::string error = "") {
  return {
      {"status", error.empty() ? 1 : 0},
      {"error_code", error.empty() ? "" : "native_privilege_lite_failed"},
      {"error", std::move(error)},
      {"data",
       nlohmann::json::array(
           {{{"hash", std::move(hash)},
             {"album_id", std::move(albumId)},
             {"relate_goods", nlohmann::json::array()},
             {"relateGoods", nlohmann::json::array()}}})},
  };
}

}  // namespace

PrivilegeService::PrivilegeService()
    : PrivilegeService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

PrivilegeService::PrivilegeService(PrivilegeHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json PrivilegeService::GetLite(std::string hash, std::string albumId) const {
  hash = Trim(std::move(hash));
  albumId = Trim(std::move(albumId));
  if (hash.empty()) {
    return EmptyPrivilege(hash, albumId, "Missing song hash");
  }

  std::string url = "http://m.kugou.com/app/i/getSongInfo.php?cmd=playInfo&hash=" + UrlEncode(hash);
  if (!albumId.empty()) {
    url += "&album_id=" + UrlEncode(albumId);
  }

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) {
    return EmptyPrivilege(hash, albumId, result.error);
  }

  if (result.statusCode < 200 || result.statusCode >= 300) {
    return EmptyPrivilege(hash, albumId, "Kugou privilege upstream returned an error");
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return EmptyPrivilege(hash, albumId, std::string("Invalid Kugou privilege JSON: ") + error.what());
  }

  const auto extra = upstream.value("extra", nlohmann::json::object());
  nlohmann::json goods = nlohmann::json::array();
  AddRelateGood(goods, extra, "128hash", "128", 1);
  AddRelateGood(goods, extra, "320hash", "320", 2);
  AddRelateGood(goods, extra, "sqhash", "flac", 3);
  AddRelateGood(goods, extra, "highhash", "high", 4);

  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data",
       nlohmann::json::array(
           {{{"hash", ReadString(upstream, "hash").empty() ? hash : ReadString(upstream, "hash")},
             {"album_id", albumId.empty() ? ReadString(upstream, "req_albumid") : albumId},
             {"albumid", ReadInt(upstream, "albumid")},
             {"album_audio_id", ReadInt(upstream, "album_audio_id")},
             {"audio_id", ReadInt(upstream, "audio_id")},
             {"privilege", ReadInt(upstream, "privilege")},
             {"pay_type", ReadInt(upstream, "pay_type")},
             {"relate_goods", goods},
             {"relateGoods", goods},
             {"raw", upstream}}})},
  };
}

}  // namespace echo::core

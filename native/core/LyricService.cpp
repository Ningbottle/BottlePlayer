#include "echo/core/LyricService.h"

#include "echo/core/StringUtils.h"

#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

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


int Base64Value(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  if (ch == '+') return 62;
  if (ch == '/') return 63;
  return -1;
}

std::string DecodeBase64(std::string_view encoded) {
  std::string output;
  int value = 0;
  int bits = -8;

  for (const char ch : encoded) {
    if (ch == '=') break;
    const int digit = Base64Value(ch);
    if (digit < 0) continue;
    value = (value << 6) + digit;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<char>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }

  return output;
}

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  return "";
}

nlohmann::json ErrorPayload(std::string code, std::string error) {
  return {
      {"status", 0},
      {"error_code", std::move(code)},
      {"error", std::move(error)},
      {"data", nullptr},
  };
}

nlohmann::json EmptySearch() {
  return {
      {"status", 1},
      {"candidates", nlohmann::json::array()},
      {"info", nlohmann::json::array()},
      {"data",
       {
           {"candidates", nlohmann::json::array()},
           {"info", nlohmann::json::array()},
       }},
  };
}

}  // namespace

LyricService::LyricService()
    : LyricService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

LyricService::LyricService(LyricHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json LyricService::Search(std::string hash) const {
  hash = Trim(std::move(hash));
  if (hash.empty()) return EmptySearch();

  const auto result = httpGet_(
      "http://lyrics.kugou.com/search?ver=1&man=yes&client=pc&hash=" + UrlEncode(hash),
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) return ErrorPayload("native_lyric_search_failed", result.error);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorPayload("native_lyric_search_failed", "Kugou lyric search returned an error");
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return ErrorPayload("native_lyric_search_invalid_json", error.what());
  }

  auto candidates = upstream.contains("candidates") ? upstream["candidates"] : nlohmann::json::array();
  auto info = upstream.contains("info") ? upstream["info"] : nlohmann::json::array();
  return {
      {"status", upstream.value("status", 1)},
      {"error", upstream.value("error", "")},
      {"candidates", candidates},
      {"info", info},
      {"data", {{"candidates", candidates}, {"info", info}}},
      {"raw", upstream},
  };
}

nlohmann::json LyricService::GetDetail(std::string id, std::string accessKey) const {
  id = Trim(std::move(id));
  accessKey = Trim(std::move(accessKey));
  if (id.empty() || accessKey.empty()) {
    return ErrorPayload("native_lyric_missing_params", "Missing lyric id or accesskey");
  }

  const auto result = httpGet_(
      "http://lyrics.kugou.com/download?ver=1&client=pc&id=" + UrlEncode(id) +
          "&accesskey=" + UrlEncode(accessKey) + "&fmt=lrc&charset=utf8",
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) return ErrorPayload("native_lyric_download_failed", result.error);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorPayload("native_lyric_download_failed", "Kugou lyric download returned an error");
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return ErrorPayload("native_lyric_download_invalid_json", error.what());
  }

  const auto content = ReadString(upstream, "content");
  const auto decoded = DecodeBase64(content);
  return {
      {"status", upstream.value("status", 1)},
      {"decodeContent", decoded},
      {"lyric", decoded},
      {"data",
       {
           {"decodeContent", decoded},
           {"lyric", decoded},
           {"id", id},
           {"accesskey", accessKey},
       }},
      {"raw", upstream},
  };
}

}  // namespace echo::core

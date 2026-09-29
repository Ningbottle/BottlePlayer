#include "echo/core/LyricService.h"

#include "echo/core/StringUtils.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#include <array>
#include <cctype>
#include <chrono>
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

nlohmann::json LyricDiagnostics(
    std::string_view identity,
    const HttpResult& result,
    long durationMs,
    std::string_view parseStatus) {
  return {
      {"hash_fingerprint", echo::diagnostics::MaskMiddle(identity, 4, 4)},
      {"upstream_http_status", result.statusCode},
      {"timed_out", result.timedOut},
      {"winhttp_error", result.error},
      {"duration_ms", durationMs},
      {"parse_status", parseStatus},
  };
}

std::string UpstreamLyricMessage(const nlohmann::json& json) {
  for (const char* key : {"error", "error_msg", "message"}) {
    if (json.contains(key) && json[key].is_string()) {
      const auto message = json[key].get<std::string>();
      if (!message.empty()) return echo::diagnostics::TruncateForLog(message, 160);
    }
  }
  return {};
}

nlohmann::json UpstreamErrorCode(const nlohmann::json& json) {
  if (json.contains("error_code")) return json["error_code"];
  if (json.contains("errcode")) return json["errcode"];
  return nullptr;
}

void AttachParsedBusinessDiagnostics(
    nlohmann::json& diagnostics,
    const nlohmann::json& upstream,
    std::string_view collectionKey) {
  diagnostics["parse_status"] = "ok";
  diagnostics["upstream_status"] =
      upstream.contains("status") ? upstream["status"] : nlohmann::json(nullptr);
  diagnostics["upstream_error_code"] = UpstreamErrorCode(upstream);
  diagnostics["upstream_message"] = UpstreamLyricMessage(upstream);
  const std::string presentKey = std::string(collectionKey) + "_present";
  const std::string typeKey = std::string(collectionKey) + "_type";
  if (!upstream.contains(collectionKey)) {
    diagnostics[presentKey] = false;
    diagnostics[typeKey] = "missing";
    diagnostics["candidate_count"] = nullptr;
    return;
  }
  diagnostics[presentKey] = true;
  diagnostics[typeKey] = upstream[collectionKey].type_name();
  diagnostics["candidate_count"] = upstream[collectionKey].is_array()
      ? nlohmann::json(upstream[collectionKey].size())
      : nlohmann::json(nullptr);
}

void CopyUpstreamErrorCode(nlohmann::json& out, const nlohmann::json& upstream) {
  const auto code = UpstreamErrorCode(upstream);
  if (!code.is_null()) out["error_code"] = code;
}

void LogLyricBoundary(const char* op, const nlohmann::json& diagnostics) {
  ECHO_LOG("LyricService", std::string(op) + " " + diagnostics.dump());
}

nlohmann::json ErrorPayload(
    std::string code,
    std::string error,
    nlohmann::json diagnostics = nlohmann::json::object()) {
  nlohmann::json payload = {
      {"status", 0},
      {"error_code", std::move(code)},
      {"error", std::move(error)},
      {"data", nullptr},
  };
  if (!diagnostics.empty()) payload["diagnostics"] = std::move(diagnostics);
  return payload;
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

  const auto started = std::chrono::steady_clock::now();
  const auto result = httpGet_(
      "http://lyrics.kugou.com/search?ver=1&man=yes&client=pc&hash=" + UrlEncode(hash),
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });
  const auto durationMs = static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());

  if (!result.error.empty()) {
    auto diagnostics = LyricDiagnostics(hash, result, durationMs, "network_error");
    diagnostics["candidate_count"] = 0;
    LogLyricBoundary("search", diagnostics);
    return ErrorPayload("native_lyric_search_failed", result.error, std::move(diagnostics));
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    auto diagnostics = LyricDiagnostics(hash, result, durationMs, "http_error");
    diagnostics["candidate_count"] = 0;
    LogLyricBoundary("search", diagnostics);
    return ErrorPayload(
        "native_lyric_search_failed",
        "Kugou lyric search returned an error",
        std::move(diagnostics));
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    auto diagnostics = LyricDiagnostics(hash, result, durationMs, "invalid_json");
    diagnostics["candidate_count"] = 0;
    LogLyricBoundary("search", diagnostics);
    return ErrorPayload("native_lyric_search_invalid_json", error.what(), std::move(diagnostics));
  }

  auto candidates = upstream.contains("candidates") ? upstream["candidates"] : nlohmann::json::array();
  auto info = upstream.contains("info") ? upstream["info"] : nlohmann::json::array();
  auto diagnostics = LyricDiagnostics(hash, result, durationMs, "ok");
  AttachParsedBusinessDiagnostics(diagnostics, upstream, "candidates");
  LogLyricBoundary("search", diagnostics);
  const auto errorText = UpstreamLyricMessage(upstream);
  nlohmann::json out = {
      {"status", upstream.contains("status") ? upstream["status"] : nlohmann::json(1)},
      {"error", errorText},
      {"candidates", candidates},
      {"info", info},
      {"data", {{"candidates", candidates}, {"info", info}}},
      {"raw", upstream},
      {"diagnostics", std::move(diagnostics)},
  };
  CopyUpstreamErrorCode(out, upstream);
  return out;
}

nlohmann::json LyricService::GetDetail(std::string id, std::string accessKey) const {
  id = Trim(std::move(id));
  accessKey = Trim(std::move(accessKey));
  if (id.empty() || accessKey.empty()) {
    return ErrorPayload("native_lyric_missing_params", "Missing lyric id or accesskey");
  }

  const auto started = std::chrono::steady_clock::now();
  const auto result = httpGet_(
      "http://lyrics.kugou.com/download?ver=1&client=pc&id=" + UrlEncode(id) +
          "&accesskey=" + UrlEncode(accessKey) + "&fmt=lrc&charset=utf8",
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });
  const auto durationMs = static_cast<long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count());

  if (!result.error.empty()) {
    auto diagnostics = LyricDiagnostics(id, result, durationMs, "network_error");
    LogLyricBoundary("download", diagnostics);
    return ErrorPayload("native_lyric_download_failed", result.error, std::move(diagnostics));
  }
  if (result.statusCode < 200 || result.statusCode >= 300) {
    auto diagnostics = LyricDiagnostics(id, result, durationMs, "http_error");
    LogLyricBoundary("download", diagnostics);
    return ErrorPayload(
        "native_lyric_download_failed",
        "Kugou lyric download returned an error",
        std::move(diagnostics));
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    auto diagnostics = LyricDiagnostics(id, result, durationMs, "invalid_json");
    LogLyricBoundary("download", diagnostics);
    return ErrorPayload("native_lyric_download_invalid_json", error.what(), std::move(diagnostics));
  }

  const auto content = ReadString(upstream, "content");
  const auto decoded = DecodeBase64(content);
  auto diagnostics = LyricDiagnostics(id, result, durationMs, "ok");
  AttachParsedBusinessDiagnostics(diagnostics, upstream, "content");
  LogLyricBoundary("download", diagnostics);
  const auto errorText = UpstreamLyricMessage(upstream);
  nlohmann::json out = {
      {"status", upstream.contains("status") ? upstream["status"] : nlohmann::json(1)},
      {"error", errorText},
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
      {"diagnostics", std::move(diagnostics)},
  };
  CopyUpstreamErrorCode(out, upstream);
  return out;
}

}  // namespace echo::core

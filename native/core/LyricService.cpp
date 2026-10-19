#include "echo/core/LyricService.h"

#include "echo/core/StringUtils.h"
#include "echo/diagnostics/EchoDiagnostics.h"
#include "echo/diagnostics/Redaction.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <optional>
#include <regex>
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
  std::uint32_t value = 0;  // modulo arithmetic; signed shifts can overflow
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

bool ValidBase64(std::string_view encoded) {
  std::size_t digits = 0, padding = 0;
  int lastDigit = 0;
  for (const auto ch : encoded) {
    if (std::isspace(static_cast<unsigned char>(ch))) continue;
    if (ch == '=') {
      if (++padding > 2) return false;
    } else {
      if (padding || Base64Value(ch) < 0) return false;
      lastDigit = Base64Value(ch);
      ++digits;
    }
  }
  if (!digits) return false;
  // Canonical zero padding bits also apply to unpadded encodings.
  if ((digits % 4 == 2 && (lastDigit & 15)) || (digits % 4 == 3 && (lastDigit & 3))) return false;
  if (!padding) return digits % 4 != 1;  // unpadded base64 is also supported
  return (digits + padding) % 4 == 0 &&
      (padding == 1 ? digits % 4 == 3 : digits % 4 == 2);
}

bool ValidUtf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    if (lead < 0x80) {
      if ((lead < 0x20 && lead != '\n' && lead != '\r' && lead != '\t') || lead == 0x7F) return false;
      continue;
    }
    const int count = lead >= 0xC2 && lead <= 0xDF ? 1 :
        lead >= 0xE0 && lead <= 0xEF ? 2 : lead >= 0xF0 && lead <= 0xF4 ? 3 : -1;
    if (count < 0 || text.size() - i < static_cast<std::size_t>(count)) return false;
    const auto next = static_cast<unsigned char>(text[i]);
    if ((lead == 0xE0 && next < 0xA0) || (lead == 0xED && next >= 0xA0) ||
        (lead == 0xF0 && next < 0x90) || (lead == 0xF4 && next >= 0x90)) return false;
    for (int j = 0; j < count; ++j) {
      if ((static_cast<unsigned char>(text[i++]) & 0xC0) != 0x80) return false;
    }
  }
  return true;
}

bool HasTimedLyrics(const std::string& text) {
  // Match the frontend LRC contract, including colon fractions and enhanced tags.
  static const std::regex timestamps(R"(\[\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?\])");
  static const std::regex wordTags(R"(<\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>)");
  std::istringstream input(text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text);
  std::string line;
  while (std::getline(input, line)) {
    if (!std::regex_search(line, timestamps)) continue;
    auto residual = Trim(std::regex_replace(std::regex_replace(line, timestamps, ""), wordTags, ""));
    std::string withoutMetadata;
    for (std::size_t i = 0; i < residual.size();) {
      if (residual[i] == '{') {
        const auto end = residual.find('}', i + 1);
        if (end != std::string::npos) { i = end + 1; continue; }
      }
      withoutMetadata.push_back(residual[i++]);
    }
    residual = std::move(withoutMetadata);
    for (const auto* separator : {"–", "—", "·"}) {
      std::size_t at;
      while ((at = residual.find(separator)) != std::string::npos) residual.erase(at, std::char_traits<char>::length(separator));
    }
    if (residual.find_first_not_of("/-. \t\r\n") != std::string::npos) return true;
  }
  return false;
}

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
#ifdef _WIN32
  const auto& item = value.at(key);
#else
  const auto& item = value.at(std::string(key));
#endif
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
      {"http_attempts", result.attemptCount},
      {"winhttp_error_code", result.winHttpErrorCode},
      {"cache_hit", false},
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

bool ZeroBusinessCode(const nlohmann::json& code) {
  return (code.is_number() && code == 0) ||
      (code.is_string() && (Trim(code.get<std::string>()).empty() || Trim(code.get<std::string>()) == "0"));
}

nlohmann::json UpstreamErrorCode(const nlohmann::json& json) {
  nlohmann::json zero = nullptr;
  for (const char* key : {"error_code", "errcode"}) {
    if (!json.contains(key) || json[key].is_null()) continue;
    const auto& code = json[key];
    if (!ZeroBusinessCode(code)) {
      return code.is_number() || code.is_string() ? code : nlohmann::json("invalid_upstream_error_code");
    }
    zero = code;
  }
  return zero;
}

bool BusinessSuccess(const nlohmann::json& upstream, bool search = false) {
  if (!upstream.is_object()) return false;
  const auto status = Trim(ReadString(upstream, "status"));
  if (!upstream.contains("status") || (status != "1" && status != "200")) return false;
  for (const char* key : {"error_code", "errcode"}) {
    if (!upstream.contains(key) || upstream[key].is_null()) continue;
    // The legacy search endpoint uses errcode=200 for successful searches.
    // Downloads use zero; do not widen their success contract with this code.
    const bool searchOk = search && status == "200" && std::string_view(key) == "errcode" &&
        Trim(ReadString(upstream, key)) == "200";
    if (!ZeroBusinessCode(upstream[key]) && !searchOk) return false;
  }
  return true;
}

std::string SearchCacheKey(std::string hash) {
  std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return "S:" + hash;
}

std::string DetailCacheKey(const std::string& id, const std::string& accessKey) {
  return "D:" + std::to_string(id.size()) + ":" + id + accessKey;
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
#ifdef _WIN32
  diagnostics[typeKey] = upstream[collectionKey].type_name();
  diagnostics["candidate_count"] = upstream[collectionKey].is_array()
      ? nlohmann::json(upstream[collectionKey].size())
      : nlohmann::json(nullptr);
#else
  const auto& collection = upstream[std::string(collectionKey)];
  diagnostics[typeKey] = collection.type_name();
  diagnostics["candidate_count"] = collection.is_array()
      ? nlohmann::json(collection.size())
      : nlohmann::json(nullptr);
#endif
}

void CopyUpstreamErrorCode(nlohmann::json& out, const nlohmann::json& upstream) {
  const auto code = UpstreamErrorCode(upstream);
  if (!code.is_null()) out["error_code"] = code;
}

void LogLyricBoundary(const char* op, const nlohmann::json& diagnostics) {
  // Do not write an upstream message (or arbitrary structured error field)
  // to disk: it can include escaped URLs/accesskeys. Keep boundary metadata.
  auto safe = diagnostics;
  safe.erase("upstream_message");
  for (const auto* key : {"upstream_status", "upstream_error_code"}) {
    if (safe.contains(key) && !safe[key].is_number() && !safe[key].is_null()) {
      const auto text = safe[key].is_string() ? safe[key].get<std::string>() : std::string{};
      if (text.empty() || text.size() > 16 || !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
          })) safe[key] = "non_numeric";
    }
  }
  ECHO_LOG("LyricService", std::string(op) + " " + safe.dump());
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

// Never persisted: lyric accesskeys and signed candidate data remain in RAM.
// Store serialized values, not duplicated JSON trees, to keep the byte budget
// meaningful. Both operations share one LRU/TTL bound.
struct LyricService::Cache {
  struct Entry {
    std::string serialized, lyricId, accessKey;
    std::chrono::steady_clock::time_point expires;
    std::uint64_t used = 0;
    std::size_t bytes = 0;
  };
  static constexpr std::size_t kMaxEntries = 128, kMaxBytes = 2 * 1024 * 1024;
  static constexpr std::size_t kMaxEntryBytes = 128 * 1024;
  std::mutex mutex;
  std::unordered_map<std::string, Entry> entries;
  std::size_t bytes = 0;
  std::uint64_t sequence = 0;

  void Erase(std::unordered_map<std::string, Entry>::iterator it) {
    bytes -= it->second.bytes;
    entries.erase(it);
  }
  void Prune(std::chrono::steady_clock::time_point now) {
    for (auto it = entries.begin(); it != entries.end();) {
      if (it->second.expires <= now) {
        const auto expired = it++;
        Erase(expired);
      } else ++it;
    }
  }
  std::optional<nlohmann::json> Get(const std::string& key, std::chrono::steady_clock::time_point now) {
    std::string serialized;
    {
      std::lock_guard lock(mutex);
      Prune(now);
      const auto it = entries.find(key);
      if (it == entries.end()) return {};
      it->second.used = ++sequence;
      serialized = it->second.serialized;
    }
    auto out = nlohmann::json::parse(serialized);
    out["diagnostics"]["cache_hit"] = true;
    out["diagnostics"]["duration_ms"] = 0;
    out["diagnostics"]["http_attempts"] = 0;
    return out;
  }
  void Put(const std::string& key, const nlohmann::json& value,
           std::chrono::steady_clock::time_point expires,
           std::chrono::steady_clock::time_point now,
           std::string lyricId = {}, std::string accessKey = {}) {
    auto serialized = value.dump();
    // Include owned string capacity and conservative container/node overhead.
    const auto cost = serialized.capacity() + key.capacity() + lyricId.capacity() +
        accessKey.capacity() + sizeof(Entry) + 64;
    if (cost > kMaxEntryBytes) return;
    std::lock_guard lock(mutex);
    Prune(now);
    if (const auto existing = entries.find(key); existing != entries.end()) Erase(existing);
    while (!entries.empty() && (entries.size() >= kMaxEntries || bytes + cost > kMaxBytes)) {
      const auto oldest = std::min_element(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.second.used < b.second.used;
      });
      Erase(oldest);
    }
    entries.emplace(key, Entry{std::move(serialized), std::move(lyricId),
        std::move(accessKey), expires, ++sequence, cost});
    bytes += cost;
  }
  void InvalidateSearch(const std::string& id, const std::string& accessKey) {
    std::lock_guard lock(mutex);
    for (auto it = entries.begin(); it != entries.end();) {
      bool matches = it->second.lyricId == id && it->second.accessKey == accessKey;
      if (!matches && it->first.starts_with("S:")) {
        // A later fallback candidate may expire too; invalidate its source list.
        const auto cached = nlohmann::json::parse(it->second.serialized);
        for (const auto& candidate : cached["candidates"]) {
          if (ReadString(candidate, "id") == id && ReadString(candidate, "accesskey") == accessKey) {
            matches = true;
            break;
          }
        }
      }
      if (it->first.starts_with("S:") && matches) {
        const auto invalid = it++;
        Erase(invalid);
      } else ++it;
    }
  }
};

LyricService::LyricService()
    : LyricService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        // Search and download each have one bounded budget including retries.
        return client.GetInteractive(url, headers, 4000);
      }) {}

LyricService::LyricService(LyricHttpGet httpGet, LyricClock clock)
    : httpGet_(std::move(httpGet)), clock_(std::move(clock)), cache_(std::make_shared<Cache>()) {
  if (!clock_) clock_ = [] { return std::chrono::steady_clock::now(); };
}

nlohmann::json LyricService::Search(std::string hash) const {
  hash = Trim(std::move(hash));
  if (hash.empty()) return EmptySearch();
  const auto cacheKey = SearchCacheKey(hash);
  if (auto cached = cache_->Get(cacheKey, clock_())) {
    LogLyricBoundary("search", (*cached)["diagnostics"]);
    return std::move(*cached);
  }

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

  if (!upstream.is_object()) {
    auto diagnostics = LyricDiagnostics(hash, result, durationMs, "invalid_shape");
    diagnostics["candidate_count"] = 0;
    LogLyricBoundary("search", diagnostics);
    return ErrorPayload("native_lyric_search_invalid_payload", "Expected lyric search object", std::move(diagnostics));
  }

  const bool success = BusinessSuccess(upstream, true);
  auto candidates = nlohmann::json::array();
  auto info = upstream.contains("info") ? upstream["info"] : nlohmann::json::array();
  auto diagnostics = LyricDiagnostics(hash, result, durationMs, "ok");
  AttachParsedBusinessDiagnostics(diagnostics, upstream, "candidates");
  if (success && (!upstream.contains("candidates") || !upstream["candidates"].is_array())) {
    diagnostics["schema_valid"] = false;
    LogLyricBoundary("search", diagnostics);
    return ErrorPayload("native_lyric_search_invalid_payload", "Invalid lyric candidates", std::move(diagnostics));
  }
  if (upstream.contains("candidates") && upstream["candidates"].is_array()) {
    for (const auto& raw : upstream["candidates"]) {
      if (!raw.is_object()) continue;
      const auto id = Trim(ReadString(raw, "id"));
      const auto key = Trim(ReadString(raw, "accesskey"));
      if (id.empty() || key.empty()) continue;
      auto candidate = raw;
      candidate["id"] = id;
      candidate["accesskey"] = key;
      candidates.push_back(std::move(candidate));
    }
    if (success && !upstream["candidates"].empty() && candidates.empty()) {
      diagnostics["schema_valid"] = false;
      LogLyricBoundary("search", diagnostics);
      return ErrorPayload("native_lyric_search_invalid_payload", "No usable lyric candidates", std::move(diagnostics));
    }
  }
  LogLyricBoundary("search", diagnostics);
  const auto errorText = UpstreamLyricMessage(upstream);
  nlohmann::json out = {
      {"status", success ? (Trim(ReadString(upstream, "status")) == "200" ? 200 : 1) : 0},
      {"error", errorText},
      {"candidates", candidates},
      {"info", info},
      {"data", {{"candidates", candidates}, {"info", info}}},
      {"raw", upstream},
      {"diagnostics", std::move(diagnostics)},
  };
  CopyUpstreamErrorCode(out, upstream);
  if (success && !candidates.empty()) {
    const auto now = clock_();
    cache_->Put(cacheKey, out, now + std::chrono::minutes(10), now,
        Trim(ReadString(candidates[0], "id")), Trim(ReadString(candidates[0], "accesskey")));
  }
  return out;
}

nlohmann::json LyricService::GetDetail(std::string id, std::string accessKey) const {
  id = Trim(std::move(id));
  accessKey = Trim(std::move(accessKey));
  if (id.empty() || accessKey.empty()) {
    return ErrorPayload("native_lyric_missing_params", "Missing lyric id or accesskey");
  }
  const auto cacheKey = DetailCacheKey(id, accessKey);
  if (auto cached = cache_->Get(cacheKey, clock_())) {
    LogLyricBoundary("download", (*cached)["diagnostics"]);
    return std::move(*cached);
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
    if (result.statusCode == 401 || result.statusCode == 403) cache_->InvalidateSearch(id, accessKey);
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

  if (!upstream.is_object()) {
    auto diagnostics = LyricDiagnostics(id, result, durationMs, "invalid_shape");
    LogLyricBoundary("download", diagnostics);
    return ErrorPayload("native_lyric_download_invalid_payload", "Expected lyric download object", std::move(diagnostics));
  }
  const bool success = BusinessSuccess(upstream);
  if (!success) cache_->InvalidateSearch(id, accessKey);

  const auto content = ReadString(upstream, "content");
  std::string decoded;
  auto diagnostics = LyricDiagnostics(id, result, durationMs, "ok");
  AttachParsedBusinessDiagnostics(diagnostics, upstream, "content");
  if (success && (!upstream.contains("content") || !upstream["content"].is_string() || !ValidBase64(content))) {
    diagnostics["schema_valid"] = false;
    cache_->InvalidateSearch(id, accessKey);
    LogLyricBoundary("download", diagnostics);
    return ErrorPayload("native_lyric_download_invalid_payload", "Invalid lyric content", std::move(diagnostics));
  }
  if (success) {
    decoded = DecodeBase64(content);
    if (!ValidUtf8(decoded) || !HasTimedLyrics(decoded)) {
      diagnostics["schema_valid"] = false;
      cache_->InvalidateSearch(id, accessKey);
      LogLyricBoundary("download", diagnostics);
      return ErrorPayload("native_lyric_download_invalid_payload", "Invalid UTF-8 or timed lyric content", std::move(diagnostics));
    }
  }
  LogLyricBoundary("download", diagnostics);
  const auto errorText = UpstreamLyricMessage(upstream);
  nlohmann::json out = {
      {"status", success ? (Trim(ReadString(upstream, "status")) == "200" ? 200 : 1) : 0},
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
  if (success) {
    const auto now = clock_();
    cache_->Put(cacheKey, out, now + std::chrono::hours(6), now);
  }
  return out;
}

}  // namespace echo::core

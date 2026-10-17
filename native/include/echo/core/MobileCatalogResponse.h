#pragma once

#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// Only the mobilecdn catalog/rank/discovery endpoints use this status=1
// contract. Lyrics and signed Android endpoints have separate success codes.
namespace echo::core::mobile_catalog {

inline nlohmann::json ParseBody(std::string_view body) {
  // Some mobilecdn endpoints wrap JSON in these exact HTML comment markers.
  // Do not extract arbitrary braces: an array/null/root mismatch stays invalid.
  constexpr std::string_view prefix = "<!--KG_TAG_RES_START-->";
  constexpr std::string_view suffix = "<!--KG_TAG_RES_END-->";
  const auto first = body.find_first_not_of(" \t\r\n");
  if (first != std::string_view::npos) body.remove_prefix(first);
  const auto last = body.find_last_not_of(" \t\r\n");
  if (last != std::string_view::npos) body = body.substr(0, last + 1);
  if (body.starts_with(prefix) && body.ends_with(suffix)) {
    body.remove_prefix(prefix.size());
    body.remove_suffix(suffix.size());
  }
  return nlohmann::json::parse(body);
}

inline std::optional<int> Integer(const nlohmann::json& item) {
  if (item.is_number_unsigned()) {
    const auto value = item.get<std::uint64_t>();
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return static_cast<int>(value);
  } else if (item.is_number_integer()) {
    const auto value = item.get<std::int64_t>();
    if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max()) return static_cast<int>(value);
  } else if (item.is_string()) {
    const auto& text = item.get_ref<const std::string&>();
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) return value;
  }
  return std::nullopt;
}

inline int ReadInt(const nlohmann::json& object, std::string_view key, int fallback = 0) {
  if (!object.is_object()) return fallback;
  const auto it = object.find(std::string(key));
  return it == object.end() ? fallback : Integer(*it).value_or(fallback);
}

inline bool Success(const nlohmann::json& response) {
  if (!response.is_object() || ReadInt(response, "status", -1) != 1) return false;
  for (const auto* field : {"error_code", "errcode"}) {
    const auto it = response.find(field);
    if (it != response.end() && Integer(*it) != std::optional<int>{0}) return false;
  }
  return true;
}

inline const nlohmann::json* Data(const nlohmann::json& response) {
  if (!response.is_object()) return nullptr;
  const auto it = response.find("data");
  return it != response.end() && it->is_object() ? &*it : nullptr;
}

inline const nlohmann::json* Info(const nlohmann::json& data) {
  const auto it = data.find("info");
  return it != data.end() && it->is_array() ? &*it : nullptr;
}

inline bool HasObjectRows(const nlohmann::json& rows) {
  if (rows.empty()) return true;
  for (const auto& row : rows) if (row.is_object()) return true;
  return false;
}

}  // namespace echo::core::mobile_catalog

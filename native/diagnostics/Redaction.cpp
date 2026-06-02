#include "echo/diagnostics/Redaction.h"

#include <algorithm>

namespace echo::diagnostics {

std::string MaskMiddle(std::string_view value, std::size_t prefix,
                       std::size_t suffix) {
  if (value.size() <= prefix + suffix) {
    return std::string(value.size(), '*');
  }
  return std::string(value.substr(0, prefix)) + "..." +
         std::string(value.substr(value.size() - suffix));
}

// Simple case-insensitive find
static std::size_t ci_find(const std::string& haystack, const std::string& needle) {
  auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
  return it == haystack.end() ? std::string::npos : static_cast<std::size_t>(std::distance(haystack.begin(), it));
}

// Find value after key= (up to space, &, ; or end)
static void mask_param(std::string& text, const std::string& key, std::size_t prefix, std::size_t suffix) {
  const std::string param = key + "=";
  std::size_t pos = 0;
  while (true) {
    std::size_t found = ci_find(text.substr(pos), param);
    if (found == std::string::npos) break;
    pos += found;  // 累加相对偏移到绝对位置
    pos += param.size();
    std::size_t end = text.find_first_of(" &;\"\n", pos);
    if (end == std::string::npos) end = text.size();
    std::size_t vlen = end - pos;
    if (vlen > 0) {
      std::string masked = MaskMiddle(text.substr(pos, vlen), prefix, suffix);
      text.replace(pos, vlen, masked);
      pos += masked.size();
    } else {
      break;  // 防止空值导致无限循环
    }
  }
}

std::string RedactSensitive(std::string_view text) {
  std::string out(text);

  // token=...
  mask_param(out, "token", 0, 0);
  // "token": "..."
  {
    const std::string key = "\"token\"";
    std::size_t pos = ci_find(out, key);
    while (pos != std::string::npos) {
      auto q1 = out.find('"', pos + key.size());
      if (q1 == std::string::npos) break;
      q1++;
      auto q2 = out.find('"', q1);
      if (q2 == std::string::npos) break;
      out.replace(q1, q2 - q1, "***");
      pos = ci_find(out.substr(q1 + 3), key);
      if (pos != std::string::npos) pos += q1 + 3;
    }
  }
  // Cookie=...
  mask_param(out, "Cookie", 0, 0);
  // KugooID=...
  mask_param(out, "KugooID", 0, 0);
  // dfid=... (prefix 3, suffix 3)
  mask_param(out, "dfid", 3, 3);
  // userid=... (prefix 2, suffix 2)
  mask_param(out, "userid", 2, 2);

  return out;
}

std::string TruncateForLog(std::string_view text, std::size_t maxBytes) {
  if (text.size() <= maxBytes) return std::string(text);
  return std::string(text.substr(0, maxBytes)) + "... truncated=true";
}

}  // namespace echo::diagnostics

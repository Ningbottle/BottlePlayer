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

// Find value after key= (up to space, &, ; or end). When prefix==suffix==0
// the caller wants total masking; MaskMiddle would yield "..." (empty prefix
// + "..." + empty suffix) which looks like truncation, so return "***" to
// signal a fully-redacted value instead.
static void mask_param(std::string& text, const std::string& key, std::size_t prefix, std::size_t suffix) {
  const std::string param = key + "=";
  const bool total_mask = (prefix == 0 && suffix == 0);
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
      std::string masked = total_mask ? "***" : MaskMiddle(text.substr(pos, vlen), prefix, suffix);
      text.replace(pos, vlen, masked);
      pos += masked.size();
    } else {
      // Empty value (e.g. "token="): skip past it but keep scanning so a later
      // non-empty occurrence of the same key is still masked. The previous
      // `break` here would abandon the whole string and leak later values.
      if (end >= text.size()) break;  // 已到字符串末尾，无法继续
      pos = end + 1;
      continue;
    }
  }
}

// Scrub the query string of every http(s):// URL found in `text`. KuGou signs
// the play URL itself (auth=/ssig=/expires=/token= in the query string) rather
// than carrying a token under a separate key, so a key-list redactor would
// leave those values in the log. We keep the query keys (so the log stays
// legible) but mask every value with "***". The URL path before `?` is left
// intact, and a URL with no query string is returned unchanged.
static void mask_url_queries(std::string& text) {
  const std::string schemes[] = {"https://", "http://"};
  const std::string kUrlEnd = " \"\n\t";

  for (const std::string& scheme : schemes) {
    std::size_t search = 0;
    while (true) {
      std::size_t url_start = ci_find(text.substr(search), scheme);
      if (url_start == std::string::npos) break;
      url_start += search;
      search = url_start + scheme.size();

      // Find where this URL ends (whitespace/quote/newline) or fall through
      // to the end of the string.
      std::size_t url_end = text.find_first_of(kUrlEnd, url_start);
      if (url_end == std::string::npos) url_end = text.size();

      // Only the query portion (after '?') needs scrubbing; the path is
      // not secret. If there is no '?', the URL has no query to scrub.
      std::size_t q = text.find('?', url_start);
      if (q == std::string::npos || q >= url_end) continue;

      // Walk each key=value pair in [q+1, url_end), masking values.
      std::size_t cur = q + 1;
      while (cur < url_end) {
        std::size_t eq = text.find('=', cur);
        std::size_t amp = text.find('&', cur);
        if (amp == std::string::npos || amp > url_end) amp = url_end;
        // No '=' before the next separator: nothing to mask (bare fragment).
        if (eq == std::string::npos || eq >= amp) {
          cur = amp + 1;
          continue;
        }
        std::size_t val = eq + 1;
        std::size_t vlen = amp - val;
        if (vlen > 0) {
          text.replace(val, vlen, "***");
          // The replacement shrank/kept the segment; re-anchor to the next '&'.
          url_end = text.find_first_of(kUrlEnd, url_start);
          if (url_end == std::string::npos) url_end = text.size();
          amp = text.find('&', val + 3);
          if (amp == std::string::npos || amp > url_end) amp = url_end;
        }
        cur = amp + 1;
      }
    }
  }
}

std::string RedactSensitive(std::string_view text) {
  std::string out(text);

  // http(s)://...?query=value — signed CDN play_url values carry auth in the
  // URL itself; scrub the query string before any key-based masking so the
  // key-list below never sees (and logs) the raw signed URL value.
  mask_url_queries(out);
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
  // t1=...
  mask_param(out, "t1", 0, 0);
  // access_token=...
  mask_param(out, "access_token", 0, 0);
  // auth_token=...
  mask_param(out, "auth_token", 0, 0);
  // session_token=...
  mask_param(out, "session_token", 0, 0);
  // secret=...
  mask_param(out, "secret", 0, 0);
  // set-cookie=...
  mask_param(out, "set-cookie", 0, 0);
  // signature=...
  mask_param(out, "signature", 0, 0);

  return out;
}

std::string TruncateForLog(std::string_view text, std::size_t maxBytes) {
  if (text.size() <= maxBytes) return std::string(text);
  return std::string(text.substr(0, maxBytes)) + "... truncated=true";
}

}  // namespace echo::diagnostics

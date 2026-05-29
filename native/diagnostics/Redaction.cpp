#include "echo/diagnostics/Redaction.h"

#include <regex>

namespace echo::diagnostics {

std::string MaskMiddle(std::string_view value, std::size_t prefix,
                       std::size_t suffix) {
  if (value.size() <= prefix + suffix) {
    return std::string(value.size(), '*');
  }
  return std::string(value.substr(0, prefix)) + "..." +
         std::string(value.substr(value.size() - suffix));
}

std::string RedactSensitive(std::string_view text) {
  std::string out(text);

  // token=...
  {
    std::regex re(R"((token=)([^\s&;]+))", std::regex::icase);
    out = std::regex_replace(out, re, "$1***");
  }
  {
    std::regex re(R"(("token"\s*:\s*")([^"]+)("))", std::regex::icase);
    out = std::regex_replace(out, re, "$1***$3");
  }

  // Cookie=...  (stop at semicolon or end-of-string)
  {
    std::regex re(R"(Cookie=[^\s;]+)", std::regex::icase);
    out = std::regex_replace(out, re, "Cookie=***");
  }

  // KugooID=...
  {
    std::regex re(R"(KugooID=[^\s&;]+)", std::regex::icase);
    out = std::regex_replace(out, re, "KugooID=***");
  }

  // dfid=...  -> dfid=abc...xyz (length >= 8)
  {
    std::regex re(R"((dfid=)([^\s&;]{8,}))", std::regex::icase);
    std::smatch m;
    std::string tmp;
    std::string::const_iterator searchStart(out.cbegin());
    while (std::regex_search(searchStart, out.cend(), m, re)) {
      tmp.append(searchStart, m[0].first);
      tmp += m[1].str();
      tmp += MaskMiddle(m[2].str(), 3, 3);
      searchStart = m[0].second;
    }
    tmp.append(searchStart, out.cend());
    out = std::move(tmp);
  }

  // userid=...  (length >= 3 digits)
  {
    std::regex re(R"((userid=)(\d{3,}))", std::regex::icase);
    std::smatch m;
    std::string tmp;
    std::string::const_iterator searchStart(out.cbegin());
    while (std::regex_search(searchStart, out.cend(), m, re)) {
      tmp.append(searchStart, m[0].first);
      tmp += m[1].str();
      tmp += MaskMiddle(m[2].str(), 2, 2);
      searchStart = m[0].second;
    }
    tmp.append(searchStart, out.cend());
    out = std::move(tmp);
  }

  return out;
}

std::string TruncateForLog(std::string_view text, std::size_t maxBytes) {
  if (text.size() <= maxBytes) return std::string(text);
  return std::string(text.substr(0, maxBytes)) + "... truncated=true";
}

}  // namespace echo::diagnostics

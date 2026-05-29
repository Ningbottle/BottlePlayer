#include "echo/core/HttpUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace echo::core {

std::string UrlDecode(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const auto hex = value.substr(i + 1, 2);
      char* end = nullptr;
      const auto code = static_cast<char>(std::strtol(std::string(hex).c_str(), &end, 16));
      decoded.push_back(code);
      i += 2;
    } else if (value[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[i]);
    }
  }
  return decoded;
}

std::string Trim(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
    value.pop_back();
  }
  while (!value.empty() && value.front() == ' ') {
    value.erase(value.begin());
  }
  return value;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

void ParseQuery(std::string_view queryString, QueryMap& query) {
  while (!queryString.empty()) {
    const auto amp = queryString.find('&');
    const auto part = queryString.substr(0, amp);
    const auto eq = part.find('=');
    if (eq != std::string_view::npos) {
      query[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
    } else if (!part.empty()) {
      query[UrlDecode(part)] = "";
    }
    if (amp == std::string_view::npos) break;
    queryString.remove_prefix(amp + 1);
  }
}

bool ParseHttpRequest(
    const std::string& raw,
    std::string& method,
    std::string& path,
    QueryMap& query,
    HeaderMap& headers) {
  std::istringstream stream(raw);
  std::string requestLine;
  if (!std::getline(stream, requestLine)) return false;

  std::istringstream requestLineStream(requestLine);
  std::string target;
  requestLineStream >> method >> target;
  if (method.empty() || target.empty()) return false;

  const auto queryStart = target.find('?');
  path = queryStart == std::string::npos ? target : target.substr(0, queryStart);
  if (queryStart != std::string::npos) {
    ParseQuery(std::string_view(target).substr(queryStart + 1), query);
  }

  std::string line;
  while (std::getline(stream, line)) {
    if (line == "\r" || line.empty()) break;
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    headers[ToLowerAscii(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
  }

  return true;
}

}  // namespace echo::core

#include "echo/core/LyricParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

namespace echo::core {
namespace {

std::string Trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

bool ParseTimestamp(std::string_view value, std::int64_t& timeMs) {
  const auto colon = value.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }

  int minutes = 0;
  int seconds = 0;
  const auto minuteResult = std::from_chars(value.data(), value.data() + colon, minutes);
  if (minuteResult.ec != std::errc{}) {
    return false;
  }

  auto secondPart = value.substr(colon + 1);
  const auto dot = secondPart.find('.');
  const auto wholeSeconds = dot == std::string_view::npos ? secondPart : secondPart.substr(0, dot);
  const auto secondResult = std::from_chars(wholeSeconds.data(), wholeSeconds.data() + wholeSeconds.size(), seconds);
  if (secondResult.ec != std::errc{}) {
    return false;
  }

  int fractionMs = 0;
  if (dot != std::string_view::npos) {
    auto fraction = secondPart.substr(dot + 1);
    if (fraction.size() > 3) {
      fraction = fraction.substr(0, 3);
    }
    int fractionValue = 0;
    const auto fractionResult =
        std::from_chars(fraction.data(), fraction.data() + fraction.size(), fractionValue);
    if (fractionResult.ec == std::errc{}) {
      if (fraction.size() == 1) {
        fractionMs = fractionValue * 100;
      } else if (fraction.size() == 2) {
        fractionMs = fractionValue * 10;
      } else {
        fractionMs = fractionValue;
      }
    }
  }

  timeMs = (static_cast<std::int64_t>(minutes) * 60 + seconds) * 1000 + fractionMs;
  return true;
}

}  // namespace

LyricDocument ParseLrc(const std::string& lrc) {
  LyricDocument document;
  document.raw = lrc;

  std::string_view remaining(lrc);
  while (!remaining.empty()) {
    const auto lineEnd = remaining.find('\n');
    auto line = lineEnd == std::string_view::npos ? remaining : remaining.substr(0, lineEnd);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    std::vector<std::int64_t> timestamps;
    while (!line.empty() && line.front() == '[') {
      const auto close = line.find(']');
      if (close == std::string_view::npos) {
        break;
      }
      std::int64_t timeMs = 0;
      if (ParseTimestamp(line.substr(1, close - 1), timeMs)) {
        timestamps.push_back(timeMs);
      }
      line.remove_prefix(close + 1);
    }

    const auto text = Trim(line);
    if (!text.empty()) {
      for (const auto timeMs : timestamps) {
        document.lines.push_back(LyricLine{timeMs, text});
      }
    }

    if (lineEnd == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(lineEnd + 1);
  }

  std::sort(document.lines.begin(), document.lines.end(), [](const LyricLine& left, const LyricLine& right) {
    return left.timeMs < right.timeMs;
  });
  return document;
}

int FindActiveLyricLine(const LyricDocument& document, std::int64_t currentMs) {
  int active = -1;
  for (std::size_t index = 0; index < document.lines.size(); ++index) {
    if (document.lines[index].timeMs > currentMs) {
      break;
    }
    active = static_cast<int>(index);
  }
  return active;
}

}  // namespace echo::core

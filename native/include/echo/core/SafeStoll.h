#pragma once

#include <string>
#include <cstdlib>
#include <cerrno>

namespace echo::core {

/// Exception-safe std::stoll replacement.
/// Returns `defaultValue` when the string is empty, non-numeric, or overflows.
inline long long SafeStoll(const std::string& s, long long defaultValue = 0) {
  if (s.empty()) return defaultValue;

  const char* begin = s.c_str();
  char* end = nullptr;
  errno = 0;
  long long result = std::strtoll(begin, &end, 10);

  if (end == begin || errno == ERANGE) return defaultValue;
  // Allow trailing whitespace but reject other trailing chars.
  while (*end != '\0') {
    if (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
      return defaultValue;
    }
    ++end;
  }
  return result;
}

/// Stricter variant for mutation boundaries (delete playlist, upload history).
/// Empty string returns 0 (meaning "no value / use default").
/// Non-numeric or overflow input returns -1 (caller should reject).
inline long long SafeStollStrict(const std::string& s) {
  if (s.empty()) return 0;

  const char* begin = s.c_str();
  char* end = nullptr;
  errno = 0;
  long long result = std::strtoll(begin, &end, 10);

  if (end == begin || errno == ERANGE) return -1;
  while (*end != '\0') {
    if (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') {
      return -1;
    }
    ++end;
  }
  return result;
}

}  // namespace echo::core

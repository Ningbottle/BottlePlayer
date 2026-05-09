#pragma once

#include <cstdint>
#include <string>

#include "echo/core/Dto.h"

namespace echo::core {

LyricDocument ParseLrc(const std::string& lrc);
int FindActiveLyricLine(const LyricDocument& document, std::int64_t currentMs);

}  // namespace echo::core

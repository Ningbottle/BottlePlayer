#pragma once

#include <string>
#include <vector>

#include "echo/core/Dto.h"

namespace echo::win32_app {

enum class LyricUiState {
  Empty,
  Ready,
};

struct LyricLineView {
  std::wstring text;
  bool active = false;
};

struct LyricViewModel {
  LyricUiState state = LyricUiState::Empty;
  std::wstring message = L"暂无歌词";
  int activeIndex = -1;
  std::vector<LyricLineView> lines;
};

LyricViewModel BuildLyricViewModel(const core::LyricDocument& document, std::int64_t currentMs);

}  // namespace echo::win32_app

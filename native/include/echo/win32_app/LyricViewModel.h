#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
std::size_t FirstVisibleLyricLine(std::size_t totalLines, int activeIndex, std::size_t visibleCount);
core::LyricDocument BuildLyricDocumentFromDetail(const nlohmann::json& response);

}  // namespace echo::win32_app

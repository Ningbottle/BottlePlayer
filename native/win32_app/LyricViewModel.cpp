#include "echo/win32_app/LyricViewModel.h"

#include <windows.h>

#include "echo/core/LyricParser.h"

namespace echo::win32_app {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), size);
  return wide;
}

std::string JsonString(const nlohmann::json& value, const char* key) {
  const auto found = value.find(key);
  return found != value.end() && found->is_string() ? found->get<std::string>() : "";
}

}  // namespace

LyricViewModel BuildLyricViewModel(const core::LyricDocument& document, std::int64_t currentMs) {
  LyricViewModel viewModel;
  if (document.lines.empty()) {
    return viewModel;
  }

  viewModel.state = LyricUiState::Ready;
  viewModel.message.clear();
  viewModel.activeIndex = core::FindActiveLyricLine(document, currentMs);
  viewModel.lines.reserve(document.lines.size());
  for (std::size_t index = 0; index < document.lines.size(); ++index) {
    viewModel.lines.push_back(LyricLineView{
        Utf8ToWide(document.lines[index].text),
        static_cast<int>(index) == viewModel.activeIndex});
  }
  return viewModel;
}

std::size_t FirstVisibleLyricLine(std::size_t totalLines, int activeIndex, std::size_t visibleCount) {
  if (totalLines == 0 || visibleCount == 0 || visibleCount >= totalLines) {
    return 0;
  }
  if (activeIndex < 0) {
    return 0;
  }

  const auto active = static_cast<std::size_t>(activeIndex);
  const auto halfWindow = visibleCount / 2;
  const auto desiredFirst = active > halfWindow ? active - halfWindow : 0;
  return std::min(desiredFirst, totalLines - visibleCount);
}

core::LyricDocument BuildLyricDocumentFromDetail(const nlohmann::json& response) {
  if (response.value("status", 0) == 0) {
    return {};
  }

  auto content = JsonString(response, "decodeContent");
  if (content.empty()) {
    content = JsonString(response, "lyric");
  }
  if (content.empty() && response.contains("data") && response["data"].is_object()) {
    content = JsonString(response["data"], "decodeContent");
  }
  if (content.empty() && response.contains("data") && response["data"].is_object()) {
    content = JsonString(response["data"], "lyric");
  }

  return content.empty() ? core::LyricDocument{} : core::ParseLrc(content);
}

}  // namespace echo::win32_app

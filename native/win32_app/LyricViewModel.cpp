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

}  // namespace echo::win32_app

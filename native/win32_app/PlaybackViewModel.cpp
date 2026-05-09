#include "echo/win32_app/PlaybackViewModel.h"

#include <string>
#include <windows.h>

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

std::string FirstString(const nlohmann::json& response, const char* topLevel, const char* dataLevel) {
  if (response.contains(topLevel) && response[topLevel].is_string()) {
    return response[topLevel].get<std::string>();
  }
  if (response.contains("data") && response["data"].is_object() &&
      response["data"].contains(dataLevel) && response["data"][dataLevel].is_string()) {
    return response["data"][dataLevel].get<std::string>();
  }
  return {};
}

}  // namespace

PlaybackViewModel BuildPlaybackViewModel(const SearchResultRow& row, const nlohmann::json& response) {
  PlaybackViewModel viewModel;
  viewModel.title = row.title;
  viewModel.artist = row.artist;
  viewModel.album = row.album;
  viewModel.duration = row.duration;

  if (response.value("status", 0) != 1) {
    viewModel.state = PlayerUiState::Error;
    viewModel.error = Utf8ToWide(response.value("error", "歌曲无法播放"));
    return viewModel;
  }

  viewModel.sourceUrl = FirstString(response, "url", "play_url");
  if (viewModel.sourceUrl.empty()) {
    viewModel.state = PlayerUiState::Error;
    viewModel.error = L"未返回播放地址";
    return viewModel;
  }

  viewModel.state = PlayerUiState::Ready;
  return viewModel;
}

}  // namespace echo::win32_app

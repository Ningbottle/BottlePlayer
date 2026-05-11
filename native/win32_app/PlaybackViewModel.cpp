#include "echo/win32_app/PlaybackViewModel.h"

#include <algorithm>
#include <cmath>
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

int DurationToSeconds(const std::wstring& value) {
  const auto colon = value.find(L':');
  if (colon == std::wstring::npos) {
    return 0;
  }
  try {
    const int minutes = std::stoi(value.substr(0, colon));
    const int seconds = std::stoi(value.substr(colon + 1));
    return std::max(0, minutes * 60 + seconds);
  } catch (...) {
    return 0;
  }
}

std::wstring FormatDuration(int seconds) {
  seconds = std::max(0, seconds);
  const int minutes = seconds / 60;
  const int remainder = seconds % 60;
  return std::to_wstring(minutes) + L":" + (remainder < 10 ? L"0" : L"") + std::to_wstring(remainder);
}

}  // namespace

PlaybackViewModel BuildPlaybackViewModel(const SearchResultRow& row, const nlohmann::json& response) {
  PlaybackViewModel viewModel;
  viewModel.title = row.title;
  viewModel.artist = row.artist;
  viewModel.album = row.album;
  viewModel.duration = row.duration;
  viewModel.coverUrl = row.coverUrl;
  viewModel.imageKey = row.imageKey;

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

std::wstring PlaybackSubtitle(const PlaybackViewModel& viewModel) {
  if (viewModel.state == PlayerUiState::Error && !viewModel.error.empty()) {
    return viewModel.error;
  }
  return viewModel.artist;
}

void ApplyPlaybackProgress(
    PlaybackViewModel& playback,
    LyricViewModel& lyric,
    const core::LyricDocument& document,
    double progress) {
  playback.progress = std::clamp(progress, 0.0, 1.0);
  const int durationSeconds = DurationToSeconds(playback.duration);
  const auto currentMs = static_cast<std::int64_t>(
      std::llround(static_cast<double>(durationSeconds) * 1000.0 * playback.progress));
  playback.current = FormatDuration(static_cast<int>(currentMs / 1000));
  lyric = BuildLyricViewModel(document, currentMs);
}

}  // namespace echo::win32_app

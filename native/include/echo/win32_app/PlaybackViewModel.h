#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "echo/win32_app/LyricViewModel.h"
#include "echo/win32_app/SearchViewModel.h"

namespace echo::win32_app {

enum class PlayerUiState {
  Idle,
  Resolving,
  Ready,
  Playing,
  Paused,
  Error,
};

struct PlaybackViewModel {
  PlayerUiState state = PlayerUiState::Idle;
  std::wstring title = L"未播放";
  std::wstring artist;
  std::wstring album;
  std::wstring duration = L"--:--";
  std::wstring current = L"00:00";
  std::wstring error;
  std::string sourceUrl;
  std::string coverUrl;
  std::string imageKey;
  double progress = 0.0;
};

PlaybackViewModel BuildPlaybackViewModel(const SearchResultRow& row, const nlohmann::json& response);
std::wstring PlaybackSubtitle(const PlaybackViewModel& viewModel);
void ApplyPlaybackProgress(
    PlaybackViewModel& playback,
    LyricViewModel& lyric,
    const core::LyricDocument& document,
    double progress);

}  // namespace echo::win32_app

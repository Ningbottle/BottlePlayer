#include "echo/win32_app/PlaybackViewModel.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
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

double DurationSeconds(const std::wstring& value) {
  const auto colon = value.find(L':');
  if (colon == std::wstring::npos) {
    return 0.0;
  }
  try {
    const int minutes = std::stoi(value.substr(0, colon));
    const int seconds = std::stoi(value.substr(colon + 1));
    return static_cast<double>(std::max(0, minutes * 60 + seconds));
  } catch (...) {
    return 0.0;
  }
}

std::string ImageKeyForCover(const std::string& coverUrl) {
  return coverUrl.empty() ? std::string{} : "remote-cover:" + coverUrl;
}

bool IsLikelyPlayable(const SearchResultRow& row) {
  return row.payType == 0 && row.privilege == 0;
}

void AppendByPlayableState(std::vector<SearchResultRow>& output, const std::vector<SearchResultRow>& rows, bool playable) {
  for (const auto& row : rows) {
    if (IsLikelyPlayable(row) == playable) {
      output.push_back(row);
    }
  }
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

  const auto responseTitle = FirstString(response, "songName", "songName");
  const auto responseArtist = FirstString(response, "singerName", "singerName");
  if (!responseTitle.empty()) {
    viewModel.title = Utf8ToWide(responseTitle);
  }
  if (!responseArtist.empty()) {
    viewModel.artist = Utf8ToWide(responseArtist);
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

void ApplyPlaybackStateSnapshot(
    PlaybackViewModel& playback,
    LyricViewModel& lyric,
    const core::LyricDocument& document,
    const core::PlaybackState& state) {
  const double durationSeconds = state.durationSeconds > 0.0 ? state.durationSeconds : DurationSeconds(playback.duration);
  if (durationSeconds > 0.0 && (playback.duration.empty() || playback.duration == L"--:--")) {
    playback.duration = FormatDuration(static_cast<int>(std::llround(durationSeconds)));
  }
  const double progress = durationSeconds > 0.0 ? state.currentSeconds / durationSeconds : 0.0;
  playback.progress = std::clamp(progress, 0.0, 1.0);
  playback.current = FormatDuration(static_cast<int>(std::llround(std::max(0.0, state.currentSeconds))));
  lyric = BuildLyricViewModel(
      document,
      static_cast<std::int64_t>(std::llround(std::max(0.0, state.currentSeconds) * 1000.0)));
}

SearchResultRow BuildSearchRowFromQueueTrack(const QueueTrack& track) {
  SearchResultRow row;
  row.title = track.title;
  row.artist = track.artist;
  row.album = track.album;
  row.duration = track.duration;
  row.coverUrl = track.coverUrl;
  row.imageKey = ImageKeyForCover(row.coverUrl);
  return row;
}

std::wstring BuildQueueTrackSearchText(const QueueTrack& track) {
  if (track.artist.empty()) {
    return track.title;
  }
  if (track.title.empty()) {
    return track.artist;
  }
  return track.title + L" " + track.artist;
}

SearchResultRow PickQueuePlaybackCandidate(const SearchResultRow& requested, const SearchViewModel& lookup) {
  const auto candidates = RankQueuePlaybackCandidates(requested, lookup);
  return candidates.empty() ? requested : candidates.front();
}

bool TryAdvancePlaybackCandidate(
    const std::vector<SearchResultRow>& candidates,
    std::size_t& currentIndex,
    SearchResultRow& pendingRow,
    PlaybackViewModel& playback,
    LyricViewModel& lyric,
    const std::wstring& lastError) {
  if (currentIndex + 1 >= candidates.size()) {
    return false;
  }

  ++currentIndex;
  pendingRow = candidates[currentIndex];
  playback.state = PlayerUiState::Resolving;
  playback.title = pendingRow.title;
  playback.artist = pendingRow.artist;
  playback.album = pendingRow.album;
  playback.duration = pendingRow.duration;
  playback.current = L"00:00";
  playback.progress = 0.0;
  playback.sourceUrl.clear();
  playback.coverUrl = pendingRow.coverUrl;
  playback.imageKey = pendingRow.imageKey;
  playback.error.clear();

  lyric = {};
  lyric.message = lastError.empty() ? L"正在尝试其他可播放版本" : L"正在尝试其他可播放版本：" + lastError;
  return true;
}

std::vector<SearchResultRow> RankQueuePlaybackCandidates(const SearchResultRow& requested, const SearchViewModel& lookup) {
  std::vector<SearchResultRow> exactMatches;
  std::vector<SearchResultRow> titleMatches;
  std::vector<SearchResultRow> fallback;

  for (const auto& candidate : lookup.rows) {
    if (candidate.hash.empty()) {
      continue;
    }
    if (candidate.title == requested.title) {
      if (requested.artist.empty() || candidate.artist == requested.artist) {
        exactMatches.push_back(candidate);
      } else {
        titleMatches.push_back(candidate);
      }
    } else {
      fallback.push_back(candidate);
    }
  }

  std::vector<SearchResultRow> ranked;
  ranked.reserve(exactMatches.size() + titleMatches.size() + fallback.size());
  AppendByPlayableState(ranked, exactMatches, true);
  AppendByPlayableState(ranked, titleMatches, true);
  AppendByPlayableState(ranked, exactMatches, false);
  AppendByPlayableState(ranked, titleMatches, false);
  AppendByPlayableState(ranked, fallback, true);
  AppendByPlayableState(ranked, fallback, false);
  return ranked;
}

}  // namespace echo::win32_app

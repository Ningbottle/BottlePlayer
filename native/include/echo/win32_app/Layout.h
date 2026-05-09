#pragma once

#include <cstddef>

namespace echo::win32_app {

struct Rect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

struct HomeLayout {
  Rect greeting;
  Rect hero;
  Rect recommendationRow;
  Rect recentList;
  Rect playlistPanel;
  Rect artistPanel;
  bool compact = false;
  bool showRecommendationRow = true;
  bool showRecentList = true;
  bool showPlaylistPanel = true;
  bool showArtistPanel = true;
  int recommendationCardCount = 5;
  int playlistCardCount = 6;
};

struct NowPlayingLayout {
  Rect tabs;
  Rect albumArea;
  Rect lyrics;
  Rect queue;
  bool showQueue = true;
};

struct PlayerBarLayout {
  Rect bar;
  Rect albumArt;
  Rect title;
  Rect artist;
  Rect favorite;
  Rect shuffle;
  Rect previous;
  Rect playPause;
  Rect next;
  Rect repeat;
  Rect currentTime;
  Rect progress;
  Rect duration;
  Rect volumeIcon;
  Rect volume;
  Rect queue;
  Rect lyric;
  bool compact = false;
  bool showSecondaryControls = true;
  bool showVolume = true;
};

struct MelodyLayout {
  Rect sidebar;
  Rect header;
  Rect content;
  Rect playerBar;
  HomeLayout home;
  NowPlayingLayout nowPlaying;
};

struct VisibleRows {
  std::size_t first = 0;
  std::size_t count = 0;
  std::size_t lastExclusive = 0;
};

struct CardStripLayout {
  int count = 0;
  float itemWidth = 0.0f;
  float itemHeight = 0.0f;
  float imageHeight = 0.0f;
  float gap = 0.0f;
};

enum class PlayerBarAction {
  None,
  OpenNowPlaying,
  OpenLyrics,
  TogglePlay,
  Previous,
  Next,
};

float DevicePixelsToDips(float pixels, float dpi);
MelodyLayout CalculateMelodyLayout(float width, float height);
PlayerBarLayout CalculatePlayerBarLayout(float width, float height);
CardStripLayout CalculateCardStripLayout(float availableWidth, int requestedCount, float availableHeight);
PlayerBarAction HitTestPlayerBar(const PlayerBarLayout& layout, float x, float y);
VisibleRows CalculateVisibleRows(std::size_t totalRows,
                                 float rowHeight,
                                 float listTop,
                                 float scrollOffset,
                                 float viewportHeight,
                                 std::size_t overscan);

}  // namespace echo::win32_app

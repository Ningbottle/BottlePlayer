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
  bool showFavorite = true;
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
  Seek,
  SetVolume,
};

struct HeaderControlsLayout {
  Rect back;
  Rect forward;
  Rect search;
  Rect avatar;
};

enum class HeaderAction {
  None,
  Back,
  Forward,
  Search,
};

enum class SidebarAction {
  None,
  Home,
  NowPlaying,
  Settings,
};

enum class HomeAction {
  None,
  PlayHero,
  OpenRecommendation,
  OpenRecent,
};

enum class NowPlayingAction {
  None,
  OverviewTab,
  LyricsTab,
};

float DevicePixelsToDips(float pixels, float dpi);
MelodyLayout CalculateMelodyLayout(float width, float height);
PlayerBarLayout CalculatePlayerBarLayout(float width, float height);
CardStripLayout CalculateCardStripLayout(float availableWidth, int requestedCount, float availableHeight);
HeaderControlsLayout CalculateHeaderControlsLayout(float width, float sidebarRight = 178.0f);
PlayerBarAction HitTestPlayerBar(const PlayerBarLayout& layout, float x, float y);
HeaderAction HitTestHeader(const HeaderControlsLayout& layout, float x, float y);
SidebarAction HitTestSidebar(float x, float y, float sidebarBottom);
HomeAction HitTestHome(const HomeLayout& layout, float x, float y);
NowPlayingAction HitTestNowPlaying(const NowPlayingLayout& layout, float x, float y);
float TrackValueFromPoint(const Rect& track, float x);
Rect CalculateAspectFitRect(const Rect& container, float sourceWidth, float sourceHeight);
Rect CalculateAspectFillRect(const Rect& container, float sourceWidth, float sourceHeight);
VisibleRows CalculateVisibleRows(std::size_t totalRows,
                                 float rowHeight,
                                 float listTop,
                                 float scrollOffset,
                                 float viewportHeight,
                                 std::size_t overscan);
float ClampScrollOffset(std::size_t totalRows, float rowHeight, float viewportHeight, float scrollOffset);
float ApplyWheelScroll(std::size_t totalRows,
                       float rowHeight,
                       float viewportHeight,
                       float currentOffset,
                       int wheelDelta);

}  // namespace echo::win32_app

#include "echo/win32_app/Layout.h"

#include <algorithm>
#include <cmath>

namespace echo::win32_app {
namespace {

float RectWidth(const Rect& rect) {
  return rect.right - rect.left;
}

float RectHeight(const Rect& rect) {
  return rect.bottom - rect.top;
}

bool IsUsable(const Rect& rect, float minWidth, float minHeight) {
  return RectWidth(rect) >= minWidth && RectHeight(rect) >= minHeight;
}

Rect EmptyRect(float x, float y) {
  return {x, y, x, y};
}

bool Contains(const Rect& rect, float x, float y) {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

}  // namespace

float DevicePixelsToDips(float pixels, float dpi) {
  if (dpi <= 0.0f) {
    return pixels;
  }
  return pixels * 96.0f / dpi;
}

MelodyLayout CalculateMelodyLayout(float width, float height) {
  constexpr float sidebarWidth = 178.0f;
  constexpr float headerHeight = 86.0f;
  constexpr float playerHeight = 96.0f;
  const bool compact = width < 1260.0f;
  const float pagePad = compact ? 20.0f : 28.0f;

  MelodyLayout layout;
  layout.sidebar = {0.0f, 0.0f, sidebarWidth, height};
  layout.header = {sidebarWidth, 0.0f, width, headerHeight};
  layout.playerBar = {0.0f, height - playerHeight, width, height};
  layout.sidebar.bottom = layout.playerBar.top;
  layout.content = {sidebarWidth, headerHeight, width, height - playerHeight};

  const float contentLeft = sidebarWidth + pagePad;
  const float contentRight = width - pagePad;
  const float contentTop = headerHeight + 18.0f;
  const float contentBottom = height - playerHeight - 20.0f;
  const bool compactHome = width < 1180.0f;
  layout.home.compact = compactHome;
  if (compactHome) {
    const bool showRecommendationRow = contentBottom - (contentTop + 272.0f) >= 156.0f;
    layout.home.showRecommendationRow = showRecommendationRow;
    layout.home.showRecentList = false;
    layout.home.showPlaylistPanel = false;
    layout.home.showArtistPanel = false;
    layout.home.recommendationCardCount = width < 980.0f ? 3 : 4;
    layout.home.playlistCardCount = 0;
    layout.home.greeting = {contentLeft, contentTop, contentRight, contentTop + 72.0f};
    layout.home.hero = {contentLeft, contentTop + 84.0f, contentRight,
                        showRecommendationRow ? contentTop + 248.0f : std::min(contentTop + 248.0f, contentBottom)};
    layout.home.recommendationRow = showRecommendationRow
                                        ? Rect{contentLeft, contentTop + 272.0f, contentRight, contentBottom}
                                        : EmptyRect(contentLeft, contentBottom);
    layout.home.recentList = EmptyRect(contentRight, contentTop);
    layout.home.playlistPanel = EmptyRect(contentLeft, contentBottom);
    layout.home.artistPanel = EmptyRect(contentRight, contentBottom);
  } else {
    const float rightColumnWidth = std::clamp(width * 0.38f, 430.0f, 620.0f);
    const float rightColumnLeft = std::max(contentLeft + 520.0f, contentRight - rightColumnWidth);
    const float leftColumnRight = rightColumnLeft - 36.0f;
    const bool showLowerPanels = contentBottom - (contentTop + 608.0f) >= 170.0f;
    layout.home.showRecommendationRow = true;
    layout.home.showRecentList = true;
    layout.home.showPlaylistPanel = showLowerPanels;
    layout.home.showArtistPanel = showLowerPanels;

    layout.home.recommendationCardCount = 5;
    layout.home.playlistCardCount = 6;
    layout.home.greeting = {contentLeft, contentTop, leftColumnRight, contentTop + 70.0f};
    layout.home.hero = {contentLeft, contentTop + 80.0f, leftColumnRight, contentTop + 304.0f};
    layout.home.recommendationRow = {
        contentLeft,
        contentTop + 340.0f,
        leftColumnRight,
        std::min(contentTop + 560.0f, contentBottom)};
    layout.home.recentList = {rightColumnLeft, contentTop + 76.0f, contentRight, contentTop + 422.0f};
    const float lowerPanelTop = std::min(contentTop + 584.0f, contentBottom - 228.0f);
    const float lowerPanelBottom = std::min(lowerPanelTop + 228.0f, contentBottom);
    layout.home.playlistPanel = showLowerPanels ? Rect{contentLeft - 14.0f, lowerPanelTop, rightColumnLeft - 30.0f, lowerPanelBottom}
                                                : EmptyRect(contentLeft, contentBottom);
    layout.home.artistPanel = showLowerPanels ? Rect{rightColumnLeft, lowerPanelTop, contentRight, lowerPanelBottom}
                                              : EmptyRect(contentRight, contentBottom);
  }

  const bool showQueue = width >= 1260.0f;
  const float queueWidth = showQueue ? std::clamp(width * 0.23f, 330.0f, 380.0f) : 0.0f;
  const float queueLeft = showQueue ? contentRight - queueWidth : contentRight;
  const float albumWidth = compact ? 330.0f : 470.0f;
  layout.nowPlaying.tabs = {contentLeft, contentTop + 6.0f, contentLeft + 220.0f, contentTop + 46.0f};
  layout.nowPlaying.albumArea = {contentLeft, contentTop + 84.0f, contentLeft + albumWidth, contentBottom};
  layout.nowPlaying.lyrics = {
      layout.nowPlaying.albumArea.right + (compact ? 32.0f : 50.0f),
      contentTop + 54.0f,
      showQueue ? queueLeft - 28.0f : contentRight,
      contentBottom};
  layout.nowPlaying.queue = showQueue ? Rect{queueLeft, contentTop + 38.0f, contentRight, contentBottom}
                                      : Rect{contentRight, contentTop + 38.0f, contentRight, contentBottom};
  layout.nowPlaying.showQueue = showQueue;

  return layout;
}

PlayerBarLayout CalculatePlayerBarLayout(float width, float height) {
  PlayerBarLayout layout;
  layout.compact = width < 1180.0f;
  layout.showVolume = width >= 1180.0f;
  layout.showSecondaryControls = width >= 1120.0f;
  const bool tiny = width < 760.0f;
  if (tiny) {
    layout.showVolume = false;
    layout.showSecondaryControls = false;
    layout.showFavorite = false;
    layout.bar = {8.0f, height - 88.0f, width - 8.0f, height - 8.0f};
    layout.albumArt = {24.0f, layout.bar.top + 16.0f, 76.0f, layout.bar.top + 68.0f};

    const float center = width * 0.5f;
    layout.previous = {center - 66.0f, layout.bar.top + 23.0f, center - 36.0f, layout.bar.top + 53.0f};
    layout.playPause = {center - 22.0f, layout.bar.top + 14.0f, center + 22.0f, layout.bar.top + 58.0f};
    layout.next = {center + 36.0f, layout.bar.top + 23.0f, center + 66.0f, layout.bar.top + 53.0f};
    layout.shuffle = EmptyRect(center - 96.0f, layout.bar.top + 36.0f);
    layout.repeat = EmptyRect(center + 96.0f, layout.bar.top + 36.0f);

    layout.lyric = {width - 50.0f, layout.bar.top + 22.0f, width - 18.0f, layout.bar.top + 56.0f};
    layout.queue = {layout.lyric.left - 44.0f, layout.bar.top + 23.0f, layout.lyric.left - 14.0f,
                    layout.bar.top + 53.0f};
    layout.volumeIcon = EmptyRect(width - 140.0f, layout.bar.top + 36.0f);
    layout.volume = EmptyRect(width - 120.0f, layout.bar.top + 49.0f);

    const float titleRight = std::max(150.0f, layout.previous.left - 14.0f);
    layout.title = {92.0f, layout.bar.top + 15.0f, titleRight, layout.bar.top + 39.0f};
    layout.artist = {92.0f, layout.bar.top + 40.0f, titleRight, layout.bar.top + 63.0f};
    layout.favorite = EmptyRect(titleRight + 8.0f, layout.bar.top + 29.0f);

    const float progressLeft = center - 118.0f;
    const float progressRight = std::min(layout.queue.left - 62.0f, center + 118.0f);
    layout.progress = {progressLeft, layout.bar.top + 69.0f, std::max(progressLeft + 120.0f, progressRight),
                       layout.bar.top + 69.0f};
    if (layout.progress.right > layout.queue.left - 62.0f) {
      layout.progress.right = layout.queue.left - 62.0f;
    }
    layout.currentTime = {layout.progress.left - 54.0f, layout.bar.top + 58.0f, layout.progress.left - 8.0f,
                          layout.bar.top + 78.0f};
    layout.duration = {layout.progress.right + 8.0f, layout.bar.top + 58.0f, layout.progress.right + 54.0f,
                       layout.bar.top + 78.0f};
    return layout;
  }

  layout.bar = {12.0f, height - 96.0f, width - 12.0f, height - 12.0f};
  layout.albumArt = {36.0f, layout.bar.top + 18.0f, 92.0f, layout.bar.top + 74.0f};

  const float center = width / 2.0f;
  const float rightToolsWidth = layout.showVolume ? 430.0f : 150.0f;
  const float rightToolsLeft = width - rightToolsWidth;
  const float compactInfoRight = std::min(center - 210.0f, rightToolsLeft - 620.0f);
  const float infoRight = layout.compact ? std::max(240.0f, compactInfoRight) : 292.0f;
  layout.title = {110.0f, layout.bar.top + 20.0f, infoRight, layout.bar.top + 46.0f};
  layout.artist = {110.0f, layout.bar.top + 48.0f, layout.title.right - 20.0f, layout.bar.top + 72.0f};
  layout.favorite = {layout.title.right + 8.0f, layout.bar.top + 29.0f, layout.title.right + 40.0f, layout.bar.top + 62.0f};

  layout.shuffle = {center - 190.0f, layout.bar.top + 36.0f, center - 156.0f, layout.bar.top + 65.0f};
  layout.previous = {center - 104.0f, layout.bar.top + 36.0f, center - 70.0f, layout.bar.top + 65.0f};
  layout.playPause = {center - 24.0f, layout.bar.top + 18.0f, center + 24.0f, layout.bar.top + 66.0f};
  layout.next = {center + 70.0f, layout.bar.top + 36.0f, center + 104.0f, layout.bar.top + 65.0f};
  layout.repeat = {center + 156.0f, layout.bar.top + 36.0f, center + 190.0f, layout.bar.top + 65.0f};

  const float rightControlLeft = layout.showVolume ? width - 420.0f : width - 128.0f;
  layout.volumeIcon = {width - 420.0f, layout.bar.top + 36.0f, width - 390.0f, layout.bar.top + 64.0f};
  layout.volume = {width - 360.0f, layout.bar.top + 49.0f, width - 200.0f, layout.bar.top + 49.0f};
  layout.queue = {rightControlLeft + (layout.showVolume ? 302.0f : 0.0f), layout.bar.top + 34.0f,
                  rightControlLeft + (layout.showVolume ? 332.0f : 30.0f), layout.bar.top + 64.0f};
  layout.lyric = {width - 64.0f, layout.bar.top + 28.0f, width - 28.0f, layout.bar.top + 64.0f};

  const float progressLeft = std::max(layout.title.right + 80.0f, center - (layout.compact ? 130.0f : 250.0f));
  const float progressRight = std::min(layout.lyric.left - 86.0f, center + (layout.compact ? 130.0f : 250.0f));
  layout.progress = {progressLeft, layout.bar.top + 73.0f, std::max(progressLeft + 220.0f, progressRight),
                     layout.bar.top + 73.0f};
  if (layout.progress.right > layout.lyric.left - 54.0f) {
    layout.progress.right = layout.lyric.left - 54.0f;
  }
  if (RectWidth(layout.progress) < 220.0f) {
    layout.progress.left = std::max(layout.favorite.right + 24.0f, layout.progress.right - 220.0f);
  }
  layout.currentTime = {layout.progress.left - 58.0f, layout.bar.top + 62.0f, layout.progress.left - 8.0f,
                        layout.bar.top + 84.0f};
  layout.duration = {layout.progress.right + 10.0f, layout.bar.top + 62.0f, layout.progress.right + 64.0f,
                     layout.bar.top + 84.0f};

  return layout;
}

CardStripLayout CalculateCardStripLayout(float availableWidth, int requestedCount, float availableHeight) {
  CardStripLayout layout;
  if (availableWidth < 140.0f || requestedCount <= 0 || availableHeight < 120.0f) {
    return layout;
  }

  layout.gap = 16.0f;
  constexpr float minWidth = 140.0f;
  constexpr float maxWidth = 190.0f;
  const int byWidth = std::max(1, static_cast<int>((availableWidth + layout.gap) / (minWidth + layout.gap)));
  layout.count = std::min(requestedCount, byWidth);
  layout.itemWidth = std::min(maxWidth, (availableWidth - layout.gap * (layout.count - 1)) / layout.count);
  layout.itemHeight = std::clamp(availableHeight, 150.0f, 210.0f);
  const float imageMax = std::max(72.0f, std::min(128.0f, layout.itemHeight - 58.0f));
  const float imageMin = std::min(96.0f, imageMax);
  layout.imageHeight = std::clamp(layout.itemWidth * 0.68f, imageMin, imageMax);
  return layout;
}

HeaderControlsLayout CalculateHeaderControlsLayout(float width, float sidebarRight) {
  const bool compact = width < 1120.0f;
  const float navLeft = sidebarRight + 34.0f;
  const float searchLeft = navLeft + 100.0f;
  const float searchRight = std::max(searchLeft + 240.0f, width - (compact ? 110.0f : 360.0f));

  HeaderControlsLayout layout;
  layout.back = {navLeft - 6.0f, 22.0f, navLeft + 30.0f, 66.0f};
  layout.forward = {navLeft + 38.0f, 22.0f, navLeft + 74.0f, 66.0f};
  layout.search = {searchLeft, 26.0f, std::min(searchRight, width - 92.0f), 66.0f};
  layout.avatar = {width - 68.0f, 44.0f, width - 30.0f, 82.0f};
  return layout;
}

PlayerBarAction HitTestPlayerBar(const PlayerBarLayout& layout, float x, float y) {
  if (!Contains(layout.bar, x, y)) return PlayerBarAction::None;
  if (Contains(layout.lyric, x, y)) return PlayerBarAction::OpenLyrics;
  if (Contains(layout.queue, x, y)) return PlayerBarAction::OpenNowPlaying;
  if (Contains(layout.playPause, x, y)) return PlayerBarAction::TogglePlay;
  if (Contains(layout.previous, x, y)) return PlayerBarAction::Previous;
  if (Contains(layout.next, x, y)) return PlayerBarAction::Next;
  const Rect progressHit{layout.progress.left, layout.progress.top - 10.0f, layout.progress.right,
                         layout.progress.top + 10.0f};
  if (Contains(progressHit, x, y)) return PlayerBarAction::Seek;
  const Rect volumeHit{layout.volume.left, layout.volume.top - 10.0f, layout.volume.right, layout.volume.top + 10.0f};
  if (layout.showVolume && Contains(volumeHit, x, y)) return PlayerBarAction::SetVolume;
  return PlayerBarAction::OpenNowPlaying;
}

HeaderAction HitTestHeader(const HeaderControlsLayout& layout, float x, float y) {
  if (Contains(layout.back, x, y)) return HeaderAction::Back;
  if (Contains(layout.forward, x, y)) return HeaderAction::Forward;
  if (Contains(layout.search, x, y)) return HeaderAction::Search;
  return HeaderAction::None;
}

SidebarAction HitTestSidebar(float x, float y, float sidebarBottom) {
  if (x < 0.0f || x > 178.0f || y < 0.0f || y > sidebarBottom) {
    return SidebarAction::None;
  }

  if (y >= 90.0f && y <= 136.0f) {
    return SidebarAction::Home;
  }
  if (y >= 430.0f && y <= 476.0f) {
    return SidebarAction::NowPlaying;
  }
  if (sidebarBottom >= 620.0f && y >= sidebarBottom - 62.0f && y <= sidebarBottom - 16.0f) {
    return SidebarAction::Settings;
  }
  return SidebarAction::None;
}

HomeAction HitTestHome(const HomeLayout& layout, float x, float y) {
  if (IsUsable(layout.hero, 260.0f, 120.0f)) {
    const float buttonTop = std::min(layout.hero.top + 154.0f, layout.hero.bottom - 54.0f);
    const Rect playButton{layout.hero.left + 28.0f, buttonTop, layout.hero.left + 118.0f, buttonTop + 40.0f};
    if (Contains(playButton, x, y)) {
      return HomeAction::PlayHero;
    }
  }

  if (layout.showRecommendationRow && IsUsable(layout.recommendationRow, 260.0f, 156.0f) &&
      Contains(layout.recommendationRow, x, y)) {
    return HomeAction::OpenRecommendation;
  }

  if (layout.showRecentList && IsUsable(layout.recentList, 300.0f, 160.0f) && Contains(layout.recentList, x, y)) {
    return HomeAction::OpenRecent;
  }

  return HomeAction::None;
}

NowPlayingAction HitTestNowPlaying(const NowPlayingLayout& layout, float x, float y) {
  const Rect overview{layout.tabs.left, layout.tabs.top, layout.tabs.left + 96.0f, layout.tabs.bottom};
  const Rect lyrics{layout.tabs.left + 112.0f, layout.tabs.top, layout.tabs.left + 184.0f, layout.tabs.bottom};
  if (Contains(overview, x, y)) return NowPlayingAction::OverviewTab;
  if (Contains(lyrics, x, y)) return NowPlayingAction::LyricsTab;
  return NowPlayingAction::None;
}

float TrackValueFromPoint(const Rect& track, float x) {
  const float width = RectWidth(track);
  if (width <= 0.0f) {
    return 0.0f;
  }
  return std::clamp((x - track.left) / width, 0.0f, 1.0f);
}

Rect CalculateAspectFitRect(const Rect& container, float sourceWidth, float sourceHeight) {
  if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || RectWidth(container) <= 0.0f || RectHeight(container) <= 0.0f) {
    return container;
  }

  const float scale = std::min(RectWidth(container) / sourceWidth, RectHeight(container) / sourceHeight);
  const float width = sourceWidth * scale;
  const float height = sourceHeight * scale;
  const float left = container.left + (RectWidth(container) - width) * 0.5f;
  const float top = container.top + (RectHeight(container) - height) * 0.5f;
  return {left, top, left + width, top + height};
}

Rect CalculateAspectFillRect(const Rect& container, float sourceWidth, float sourceHeight) {
  if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || RectWidth(container) <= 0.0f || RectHeight(container) <= 0.0f) {
    return container;
  }

  const float scale = std::max(RectWidth(container) / sourceWidth, RectHeight(container) / sourceHeight);
  const float width = sourceWidth * scale;
  const float height = sourceHeight * scale;
  const float left = container.left + (RectWidth(container) - width) * 0.5f;
  const float top = container.top + (RectHeight(container) - height) * 0.5f;
  return {left, top, left + width, top + height};
}

VisibleRows CalculateVisibleRows(std::size_t totalRows,
                                 float rowHeight,
                                 float listTop,
                                 float scrollOffset,
                                 float viewportHeight,
                                 std::size_t overscan) {
  (void)listTop;
  if (totalRows == 0 || rowHeight <= 0.0f || viewportHeight <= 0.0f) {
    return {};
  }

  const float normalizedOffset = std::max(0.0f, scrollOffset);
  const auto firstVisible = static_cast<std::size_t>(std::floor(normalizedOffset / rowHeight));
  const auto first = firstVisible > overscan ? firstVisible - overscan : 0;
  const auto visibleCount = static_cast<std::size_t>(std::ceil(viewportHeight / rowHeight)) + overscan * 2;
  const auto lastExclusive = std::min(totalRows, first + visibleCount);
  return VisibleRows{first, lastExclusive - first, lastExclusive};
}

float ClampScrollOffset(std::size_t totalRows, float rowHeight, float viewportHeight, float scrollOffset) {
  if (totalRows == 0 || rowHeight <= 0.0f || viewportHeight <= 0.0f) {
    return 0.0f;
  }

  const float contentHeight = static_cast<float>(totalRows) * rowHeight;
  const float maxOffset = std::max(0.0f, contentHeight - viewportHeight);
  return std::clamp(scrollOffset, 0.0f, maxOffset);
}

float ApplyWheelScroll(std::size_t totalRows,
                       float rowHeight,
                       float viewportHeight,
                       float currentOffset,
                       int wheelDelta) {
  constexpr float rowsPerWheel = 3.0f;
  const float deltaRows = -static_cast<float>(wheelDelta) / 120.0f * rowsPerWheel;
  return ClampScrollOffset(totalRows, rowHeight, viewportHeight, currentOffset + deltaRows * rowHeight);
}

}  // namespace echo::win32_app

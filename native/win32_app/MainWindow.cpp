#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "echo/core/BackendFacade.h"
#include "echo/core/LyricParser.h"
#include "echo/playback/PlaybackController.h"
#include "echo/win32_app/Layout.h"
#include "echo/win32_app/LyricViewModel.h"
#include "echo/win32_app/PlaybackViewModel.h"
#include "echo/win32_app/SearchViewModel.h"

namespace echo::win32_app {
namespace {

template <typename T>
void SafeRelease(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

struct Palette {
  D2D1_COLOR_F bg = D2D1::ColorF(0.965f, 0.952f, 0.925f);
  D2D1_COLOR_F panel = D2D1::ColorF(0.985f, 0.976f, 0.955f, 0.88f);
  D2D1_COLOR_F panelStrong = D2D1::ColorF(0.93f, 0.90f, 0.85f, 0.72f);
  D2D1_COLOR_F line = D2D1::ColorF(0.70f, 0.67f, 0.60f, 0.28f);
  D2D1_COLOR_F text = D2D1::ColorF(0.08f, 0.08f, 0.075f);
  D2D1_COLOR_F muted = D2D1::ColorF(0.34f, 0.34f, 0.32f);
  D2D1_COLOR_F faint = D2D1::ColorF(0.50f, 0.50f, 0.46f);
  D2D1_COLOR_F accent = D2D1::ColorF(0.10f, 0.37f, 0.72f);
  D2D1_COLOR_F accentDark = D2D1::ColorF(0.13f, 0.30f, 0.45f);
  D2D1_COLOR_F white = D2D1::ColorF(1.0f, 1.0f, 1.0f);
};

struct TextStyle {
  float size = 14.0f;
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
  DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
};

std::wstring ToWideAscii(const std::string& value) {
  return std::wstring(value.begin(), value.end());
}

std::wstring DeviceTail(const std::string& value) {
  if (value.empty()) return L"";
  const auto start = value.size() > 6 ? value.size() - 6 : 0;
  return ToWideAscii(value.substr(start));
}

D2D1_RECT_F ToD2DRect(const Rect& rect) {
  return D2D1::RectF(std::round(rect.left), std::round(rect.top), std::round(rect.right), std::round(rect.bottom));
}

D2D1_RECT_F PixelRect(float left, float top, float right, float bottom) {
  return D2D1::RectF(std::round(left), std::round(top), std::round(right), std::round(bottom));
}

bool IsUsable(const Rect& rect, float minWidth = 2.0f, float minHeight = 2.0f) {
  return rect.right - rect.left >= minWidth && rect.bottom - rect.top >= minHeight;
}

bool Contains(D2D1_RECT_F rect, float x, float y) {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

void EnableDpiAwareness() {
  using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  auto* user32 = GetModuleHandleW(L"user32.dll");
  auto* setAwareness = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
      user32 ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr);
  if (setAwareness && setAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    return;
  }
  SetProcessDPIAware();
}

core::LyricDocument DemoLyricDocument() {
  return core::ParseLrc(
      "[00:30.00]窗外的阳光刚好洒在肩上\n"
      "[00:45.00]微风轻轻吹过了操场\n"
      "[01:00.00]你笑着说想要去看海\n"
      "[01:15.00]我们的好明天一起出发\n"
      "[01:42.00]想把你写进日记的每一页\n"
      "[01:56.00]记录下青春最美的画面\n"
      "[02:12.00]不怕未来会有多少变迁\n"
      "[02:28.00]只想今天好好陪在你身边\n"
      "[02:44.00]雨后的天空出现彩虹\n"
      "[03:00.00]我们牵着手走在街头\n"
      "[03:18.00]时间慢慢流过不再匆匆\n"
      "[03:36.00]那些回忆都变得温柔\n");
}

class MainWindow {
 public:
  MainWindow() : lyricDocument_(DemoLyricDocument()) {
    lyricView_ = BuildLyricViewModel(lyricDocument_, playbackPositionMs_);
  }

  ~MainWindow() {
    DiscardDeviceResources();
    SafeRelease(writeFactory_);
    SafeRelease(d2dFactory_);
  }

  bool Create(HINSTANCE instance, int showCommand) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"BottleMusicNativeWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"BottleMusic",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1600,
        1060,
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) return false;
    ShowWindow(hwnd_, showCommand);
    FitToWorkArea();
    UpdateWindow(hwnd_);
    return true;
  }

 private:
  enum class Surface { Home, NowPlaying, Search };
  static constexpr UINT_PTR kBackendPollTimer = 1;
  static constexpr UINT_PTR kStartupTimer = 2;

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* window = nullptr;
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
      window = static_cast<MainWindow*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
      window->hwnd_ = hwnd;
    } else {
      window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!window) return DefWindowProcW(hwnd, message, wParam, lParam);
    return window->HandleMessage(message, wParam, lParam);
  }

  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
      case WM_CREATE:
        if (!InitializeGraphics()) return -1;
        SetTimer(hwnd_, kStartupTimer, 10, nullptr);
        return 0;
      case WM_SIZE:
        ResizeRenderTarget();
        return 0;
      case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_,
                     nullptr,
                     suggested->left,
                     suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeRenderTarget();
        return 0;
      }
      case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const float dpiScale = WindowDpi() / 96.0f;
        RECT minClient{
            0,
            0,
            static_cast<LONG>(std::round(900.0f * dpiScale)),
            static_cast<LONG>(std::round(640.0f * dpiScale))};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        AdjustWindowRectEx(&minClient, style, FALSE, exStyle);
        info->ptMinTrackSize.x = minClient.right - minClient.left;
        info->ptMinTrackSize.y = minClient.bottom - minClient.top;
        return 0;
      }
      case WM_TIMER:
        if (wParam == kStartupTimer) {
          KillTimer(hwnd_, kStartupTimer);
          StartBackend();
        } else if (wParam == kBackendPollTimer) {
          PollBackend();
        }
        return 0;
      case WM_KEYDOWN:
        if (wParam == '1') surface_ = Surface::Home;
        if (wParam == '2') surface_ = Surface::NowPlaying;
        if (wParam == 'L') surface_ = Surface::NowPlaying;
        if (wParam == 'S') BeginSearch(L"晴天", "晴天");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_LBUTTONUP:
        HandleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
      case WM_PAINT:
        Paint();
        return 0;
      case WM_DESTROY:
        KillTimer(hwnd_, kStartupTimer);
        KillTimer(hwnd_, kBackendPollTimer);
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
  }

  bool InitializeGraphics() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_))) {
      return false;
    }
    return SUCCEEDED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&writeFactory_)));
  }

  void StartBackend() {
    if (backend_ || backendFuture_.valid()) {
      return;
    }

    try {
      deviceStatus_ = L"设备初始化中";
      backendFuture_ = std::async(std::launch::async, [] {
        return core::CreateBackendFacade();
      });
      SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    } catch (...) {
      deviceStatus_ = L"设备初始化失败";
    }
  }

  void FitToWorkArea() {
    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
      return;
    }

    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;
    const int width = std::min(static_cast<int>(windowRect.right - windowRect.left), workWidth);
    const int height = std::min(static_cast<int>(windowRect.bottom - windowRect.top), workHeight);
    const int x = workArea.left + std::max(0, (workWidth - width) / 2);
    const int y = workArea.top + std::max(0, (workHeight - height) / 2);
    MoveWindow(hwnd_, x, y, width, height, FALSE);
  }

  float WindowDpi() const {
    if (!hwnd_) {
      return 96.0f;
    }
    const UINT dpi = GetDpiForWindow(hwnd_);
    return dpi == 0 ? 96.0f : static_cast<float>(dpi);
  }

  float DevicePxToDip(float value) const {
    return DevicePixelsToDips(value, WindowDpi());
  }

  D2D1_SIZE_F CurrentClientDipSize() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return D2D1::SizeF(
        DevicePxToDip(static_cast<float>(rc.right - rc.left)),
        DevicePxToDip(static_cast<float>(rc.bottom - rc.top)));
  }

  void BeginSearch(std::wstring displayKeyword, std::string keyword) {
    surface_ = Surface::Search;
    searchKeyword_ = std::move(keyword);
    searchView_ = SearchViewModel{};
    searchView_.keyword = std::move(displayKeyword);
    searchView_.state = SearchState::Loading;
    searchView_.message = L"正在搜索";

    try {
      if (!backend_) {
        StartBackend();
        searchView_.state = SearchState::Error;
        searchView_.message = L"后端初始化中，请稍后再试";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      searchFuture_ = backend_->SearchSongs(searchKeyword_, 1, 30);
      SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    } catch (...) {
      searchView_.state = SearchState::Error;
      searchView_.message = L"搜索启动失败";
    }
  }

  void BeginResolveAndPlay(std::size_t rowIndex) {
    if (rowIndex >= searchView_.rows.size()) {
      return;
    }

    pendingPlaybackRow_ = searchView_.rows[rowIndex];
    playerView_ = PlaybackViewModel{};
    playerView_.state = PlayerUiState::Resolving;
    playerView_.title = pendingPlaybackRow_.title;
    playerView_.artist = pendingPlaybackRow_.artist;
    playerView_.album = pendingPlaybackRow_.album;
    playerView_.duration = pendingPlaybackRow_.duration;
    playerView_.current = L"00:00";

    try {
      if (!backend_) {
        StartBackend();
        playerView_.state = PlayerUiState::Error;
        playerView_.error = L"后端初始化中，请稍后再试";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      songUrlFuture_ = backend_->ResolveSongUrl(pendingPlaybackRow_.hash, "");
      SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    } catch (...) {
      playerView_.state = PlayerUiState::Error;
      playerView_.error = L"播放启动失败";
    }

    surface_ = Surface::NowPlaying;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void PollBackend() {
    bool hasPendingWork = false;

    if (backendFuture_.valid()) {
      if (backendFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          backend_ = backendFuture_.get();
          if (backend_) {
            deviceFuture_ = backend_->EnsureDeviceReady();
            hasPendingWork = true;
          }
        } catch (...) {
          deviceStatus_ = L"设备初始化失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (deviceFuture_.valid()) {
      if (deviceFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          device_ = deviceFuture_.get();
          deviceStatus_ = L"设备已就绪 · MID " + DeviceTail(device_.mid);
        } catch (...) {
          deviceStatus_ = L"设备初始化失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (searchFuture_.valid()) {
      if (searchFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          searchView_ = BuildSearchViewModel(searchKeyword_, searchFuture_.get());
        } catch (...) {
          searchView_.state = SearchState::Error;
          searchView_.message = L"搜索请求失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (songUrlFuture_.valid()) {
      if (songUrlFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          playerView_ = BuildPlaybackViewModel(pendingPlaybackRow_, songUrlFuture_.get());
          if (playerView_.state == PlayerUiState::Ready && EnsurePlaybackReady() &&
              playback_.PlayUrl(playerView_.sourceUrl)) {
            playerView_.state = PlayerUiState::Playing;
            playbackPositionMs_ = 102000;
            playerView_.current = L"01:42";
            playerView_.progress = 0.38;
            lyricView_ = BuildLyricViewModel(lyricDocument_, playbackPositionMs_);
          } else if (playerView_.state == PlayerUiState::Ready) {
            playerView_.state = PlayerUiState::Error;
            playerView_.error = L"播放地址打开失败";
          }
        } catch (...) {
          playerView_.state = PlayerUiState::Error;
          playerView_.error = L"播放请求失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (!hasPendingWork) {
      KillTimer(hwnd_, kBackendPollTimer);
    }
  }

  bool EnsurePlaybackReady() {
    if (playbackInitialized_) {
      return true;
    }
    playbackInitialized_ = playback_.Initialize();
    return playbackInitialized_;
  }

  HRESULT CreateDeviceResources() {
    if (renderTarget_) return S_OK;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const auto size = D2D1::SizeU(
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top));
    const HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
            0.0f,
            0.0f,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT),
        D2D1::HwndRenderTargetProperties(hwnd_, size),
        &renderTarget_);
    if (SUCCEEDED(hr)) {
      renderTarget_->SetDpi(WindowDpi(), WindowDpi());
      renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
      renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    return hr;
  }

  void DiscardDeviceResources() {
    SafeRelease(renderTarget_);
  }

  void ResizeRenderTarget() {
    if (!renderTarget_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    renderTarget_->SetDpi(WindowDpi(), WindowDpi());
    renderTarget_->Resize(D2D1::SizeU(
        static_cast<UINT32>(rc.right - rc.left),
        static_cast<UINT32>(rc.bottom - rc.top)));
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void HandleClick(int x, int y) {
    const float dipX = DevicePxToDip(static_cast<float>(x));
    const float dipY = DevicePxToDip(static_cast<float>(y));
    const auto playerAction =
        HitTestPlayerBar(CalculatePlayerBarLayout(clientWidth_, clientHeight_), dipX, dipY);
    if (playerAction == PlayerBarAction::OpenLyrics || playerAction == PlayerBarAction::OpenNowPlaying) {
      surface_ = Surface::NowPlaying;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::TogglePlay) {
      if (playerView_.state == PlayerUiState::Playing) {
        playback_.Pause();
        playerView_.state = PlayerUiState::Paused;
      } else if (playerView_.state == PlayerUiState::Ready || playerView_.state == PlayerUiState::Paused ||
                 playerView_.state == PlayerUiState::Idle) {
        playback_.Resume();
        playerView_.state = PlayerUiState::Playing;
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }

    if (dipX < 178 && dipY > 88 && dipY < 150) {
      surface_ = Surface::Home;
    } else if (dipX < 178 && dipY > 390 && dipY < 610) {
      surface_ = Surface::NowPlaying;
    } else if (dipX > clientWidth_ - 150 && dipY > clientHeight_ - 88) {
      surface_ = Surface::NowPlaying;
    } else if (surface_ == Surface::Search) {
      const auto row = SearchRowFromPoint(dipX, dipY);
      if (row != kNoRow) {
        BeginResolveAndPlay(row);
        return;
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  static constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);

  std::size_t SearchRowFromPoint(float x, float y) const {
    if (searchView_.state != SearchState::Ready) {
      return kNoRow;
    }

    const auto content = layout_.content;
    const float left = content.left + 28.0f;
    const float top = content.top + 28.0f;
    const float right = content.right - 28.0f;
    const float bottom = content.bottom - 24.0f;
    const float listTop = top + 166.0f;
    constexpr float rowHeight = 58.0f;

    if (x < left || x > right || y < listTop || y > bottom) {
      return kNoRow;
    }

    const auto index = static_cast<std::size_t>((y - listTop + searchScrollOffset_) / rowHeight);
    return index < searchView_.rows.size() ? index : kNoRow;
  }

  void Paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);

    if (SUCCEEDED(CreateDeviceResources())) {
      const auto size = CurrentClientDipSize();
      clientWidth_ = size.width;
      clientHeight_ = size.height;
      layout_ = CalculateMelodyLayout(clientWidth_, clientHeight_);

      renderTarget_->BeginDraw();
      renderTarget_->Clear(palette_.bg);

      DrawSidebar();
      DrawHeader();
      renderTarget_->PushAxisAlignedClip(ToD2DRect(layout_.content), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      if (surface_ == Surface::Home) {
        DrawHome();
      } else if (surface_ == Surface::NowPlaying) {
        DrawNowPlaying();
      } else {
        DrawSearch();
      }
      renderTarget_->PopAxisAlignedClip();
      DrawPlayerBar();

      const HRESULT hr = renderTarget_->EndDraw();
      if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
      }
    }

    EndPaint(hwnd_, &ps);
  }

  ID2D1SolidColorBrush* Brush(D2D1_COLOR_F color) {
    ID2D1SolidColorBrush* brush = nullptr;
    renderTarget_->CreateSolidColorBrush(color, &brush);
    return brush;
  }

  void FillRect(D2D1_RECT_F rect, D2D1_COLOR_F color) {
    auto* brush = Brush(color);
    renderTarget_->FillRectangle(rect, brush);
    SafeRelease(brush);
  }

  void FillRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) {
    auto* brush = Brush(color);
    renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
    SafeRelease(brush);
  }

  void StrokeRound(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color, float width = 1.0f) {
    auto* brush = Brush(color);
    renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush, width);
    SafeRelease(brush);
  }

  void StrokeLine(float x1, float y1, float x2, float y2, D2D1_COLOR_F color, float width = 1.0f) {
    auto* brush = Brush(color);
    const float pixelOffset = width <= 1.0f ? 0.5f : 0.0f;
    renderTarget_->DrawLine(D2D1::Point2F(std::round(x1) + pixelOffset, std::round(y1) + pixelOffset),
                            D2D1::Point2F(std::round(x2) + pixelOffset, std::round(y2) + pixelOffset),
                            brush,
                            width);
    SafeRelease(brush);
  }

  void Text(const std::wstring& value, D2D1_RECT_F rect, D2D1_COLOR_F color, TextStyle style) {
    IDWriteTextFormat* format = nullptr;
    writeFactory_->CreateTextFormat(
        L"Microsoft YaHei UI",
        nullptr,
        style.weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        style.size,
        L"zh-CN",
        &format);
    format->SetTextAlignment(style.align);
    format->SetParagraphAlignment(style.paragraph);
    auto* brush = Brush(color);
    renderTarget_->DrawTextW(
        value.c_str(),
        static_cast<UINT32>(value.size()),
        format,
        rect,
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    SafeRelease(brush);
    SafeRelease(format);
  }

  void Circle(float cx, float cy, float radius, D2D1_COLOR_F color) {
    auto* brush = Brush(color);
    renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush);
    SafeRelease(brush);
  }

  void FillTriangle(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c, D2D1_COLOR_F color) {
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geometry))) return;

    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(geometry->Open(&sink))) {
      sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
      sink->AddLine(b);
      sink->AddLine(c);
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      sink->Close();
    }
    SafeRelease(sink);

    auto* brush = Brush(color);
    renderTarget_->FillGeometry(geometry, brush);
    SafeRelease(brush);
    SafeRelease(geometry);
  }

  void DrawPlayTriangle(D2D1_RECT_F rect, D2D1_COLOR_F color, bool right = true) {
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float w = (rect.right - rect.left) * 0.34f;
    const float h = (rect.bottom - rect.top) * 0.42f;
    if (right) {
      FillTriangle(D2D1::Point2F(cx - w * 0.45f, cy - h), D2D1::Point2F(cx - w * 0.45f, cy + h),
                   D2D1::Point2F(cx + w * 0.72f, cy), color);
    } else {
      FillTriangle(D2D1::Point2F(cx + w * 0.45f, cy - h), D2D1::Point2F(cx + w * 0.45f, cy + h),
                   D2D1::Point2F(cx - w * 0.72f, cy), color);
    }
  }

  void DrawPauseIcon(D2D1_RECT_F rect, D2D1_COLOR_F color) {
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float h = (rect.bottom - rect.top) * 0.40f;
    const float barW = 4.0f;
    FillRound(D2D1::RectF(cx - 8.0f, cy - h, cx - 8.0f + barW, cy + h), 1.5f, color);
    FillRound(D2D1::RectF(cx + 4.0f, cy - h, cx + 4.0f + barW, cy + h), 1.5f, color);
  }

  void DrawSidebar() {
    const float sidebarBottom = layout_.sidebar.bottom;
    FillRect(D2D1::RectF(0, 0, 178, sidebarBottom), D2D1::ColorF(0.945f, 0.932f, 0.900f, 0.84f));
    StrokeLine(178, 0, 178, sidebarBottom, palette_.line);
    renderTarget_->PushAxisAlignedClip(D2D1::RectF(0, 0, 178, sidebarBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    Text(L"♪ BottleMusic", D2D1::RectF(28, 34, 168, 66), palette_.accentDark,
         {21, DWRITE_FONT_WEIGHT_SEMI_BOLD});

    struct Item {
      const wchar_t* icon;
      const wchar_t* label;
      bool active;
    };
    const std::array<Item, 10> nav = {{
        {L"⌂", L"首页", surface_ == Surface::Home},
        {L"◎", L"发现", false},
        {L"◌", L"电台", false},
        {L"▣", L"视频", false},
        {L"♪", L"歌曲", false},
        {L"⊙", L"专辑", false},
        {L"♙", L"歌手", false},
        {L"≡", L"播放列表", surface_ == Surface::NowPlaying},
        {L"♡", L"收藏夹", false},
        {L"↧", L"下载管理", false},
    }};

    float y = 98;
    for (std::size_t i = 0; i < nav.size(); ++i) {
      if (i == 4) {
        StrokeLine(30, y - 10, 150, y - 10, palette_.line);
        Text(L"你的音乐", D2D1::RectF(31, y + 2, 150, y + 24), palette_.faint, {12});
        y += 42;
      }
      if (nav[i].active) {
        FillRound(D2D1::RectF(14, y - 8, 164, y + 30), 8, palette_.panelStrong);
      }
      Text(nav[i].icon, D2D1::RectF(34, y, 52, y + 22), nav[i].active ? palette_.accent : palette_.muted,
           {15});
      Text(nav[i].label, D2D1::RectF(63, y, 150, y + 24), nav[i].active ? palette_.accent : palette_.text,
           {14, nav[i].active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL});
      y += 44;
    }

    StrokeLine(30, 579, 150, 579, palette_.line);
    Text(L"播放列表        +", D2D1::RectF(31, 600, 155, 626), palette_.muted, {13});

    const std::array<const wchar_t*, 5> playlists = {
        L"Chill Vibes", L"清晨旋律", L"健身动力", L"工作学习", L"经典怀旧"};
    const std::array<D2D1_COLOR_F, 5> colors = {{
        D2D1::ColorF(0.12f, 0.31f, 0.55f),
        D2D1::ColorF(0.82f, 0.58f, 0.16f),
        D2D1::ColorF(0.22f, 0.50f, 0.55f),
        D2D1::ColorF(0.38f, 0.55f, 0.64f),
        D2D1::ColorF(0.54f, 0.62f, 0.35f),
    }};
    y = 640;
    for (std::size_t i = 0; i < playlists.size(); ++i) {
      FillRound(D2D1::RectF(31, y - 5, 55, y + 19), 4, colors[i]);
      Text(playlists[i], D2D1::RectF(64, y - 3, 156, y + 22), palette_.text, {13});
      y += 39;
    }

    if (sidebarBottom >= 620.0f) {
      Text(L"⚙  设置", D2D1::RectF(34, sidebarBottom - 54.0f, 150, sidebarBottom - 22.0f), palette_.muted, {14});
    }
    renderTarget_->PopAxisAlignedClip();
  }

  void DrawHeader() {
    const bool compact = clientWidth_ < 1120.0f;
    const float navLeft = layout_.sidebar.right + 34.0f;
    Text(L"‹", D2D1::RectF(navLeft, 25, navLeft + 26, 60), palette_.text, {36});
    Text(L"›", D2D1::RectF(navLeft + 44, 25, navLeft + 70, 60), palette_.text, {36});

    const float searchLeft = navLeft + 100.0f;
    const float searchRight = std::max(searchLeft + 240.0f, clientWidth_ - (compact ? 110.0f : 360.0f));
    const auto searchRect = D2D1::RectF(searchLeft, 26, std::min(searchRight, clientWidth_ - 92.0f), 66);
    FillRound(searchRect, 8, D2D1::ColorF(0.95f, 0.94f, 0.91f, 0.78f));
    StrokeRound(searchRect, 8, palette_.line);
    Text(L"搜索音乐、歌手、专辑或歌词", D2D1::RectF(searchRect.left + 26, 38, searchRect.right - 58, 62),
         palette_.faint, {13});
    Text(L"⌕", D2D1::RectF(searchRect.right - 36, 36, searchRect.right - 10, 62), palette_.text, {18});
    if (!compact) {
      Text(deviceStatus_, D2D1::RectF(clientWidth_ - 292, 38, clientWidth_ - 82, 62), palette_.muted,
           {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
    }
    Circle(clientWidth_ - 49, 62, 18, D2D1::ColorF(0.25f, 0.20f, 0.16f));
  }

  void DrawHome() {
    const auto& home = layout_.home;
    const float titleSize = home.compact ? 24.0f : 27.0f;
    const float actionWidth = home.compact ? 130.0f : 137.0f;
    const float actionRight = home.greeting.right;
    Text(L"早上好，开启美好的一天",
         D2D1::RectF(home.greeting.left, home.greeting.top, actionRight - actionWidth - 18.0f,
                     home.greeting.top + 38.0f),
         palette_.text, {titleSize, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"用音乐点亮你的每一刻",
         D2D1::RectF(home.greeting.left, home.greeting.top + 42.0f, home.greeting.right,
                     home.greeting.top + 70.0f),
         palette_.muted, {14});
    DrawButton(D2D1::RectF(actionRight - actionWidth, home.greeting.top + 18.0f, actionRight,
                           home.greeting.top + 56.0f),
               L"✦ 个性化推荐", false);

    if (IsUsable(home.hero, 260.0f, 120.0f)) {
      DrawHero(ToD2DRect(home.hero));
    }

    if (home.showRecommendationRow && IsUsable(home.recommendationRow, 260.0f, 156.0f)) {
      Text(L"为你推荐", D2D1::RectF(home.recommendationRow.left, home.recommendationRow.top,
                                home.recommendationRow.left + 160.0f, home.recommendationRow.top + 28.0f),
           palette_.text, {18, DWRITE_FONT_WEIGHT_BOLD});
      Text(L"查看全部", D2D1::RectF(home.recommendationRow.right - 72.0f, home.recommendationRow.top + 2.0f,
                                  home.recommendationRow.right, home.recommendationRow.top + 28.0f),
           palette_.accent, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      const float cardsTop = home.recommendationRow.top + 38.0f;
      const float cardHeight = std::max(118.0f, home.recommendationRow.bottom - cardsTop - 8.0f);
      DrawPlaylistCards(home.recommendationRow.left, cardsTop, home.recommendationRow.right,
                        home.recommendationCardCount, cardHeight);
    }

    if (home.showRecentList && IsUsable(home.recentList, 300.0f, 160.0f)) {
      Text(L"最近播放", D2D1::RectF(home.recentList.left + 18.0f, home.recentList.top - 44.0f,
                                  home.recentList.right - 80.0f, home.recentList.top - 16.0f),
           palette_.text, {18, DWRITE_FONT_WEIGHT_BOLD});
      Text(L"查看全部", D2D1::RectF(home.recentList.right - 80.0f, home.recentList.top - 42.0f,
                                  home.recentList.right, home.recentList.top - 16.0f),
           palette_.accent, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      DrawRecentList(ToD2DRect(home.recentList));
    }

    if (home.showPlaylistPanel && IsUsable(home.playlistPanel, 320.0f, 160.0f)) {
      FillRound(ToD2DRect(home.playlistPanel), 8, palette_.panel);
      StrokeRound(ToD2DRect(home.playlistPanel), 8, palette_.line);
      Text(L"推荐歌单", D2D1::RectF(home.playlistPanel.left + 14.0f, home.playlistPanel.top + 18.0f,
                                  home.playlistPanel.left + 220.0f, home.playlistPanel.top + 48.0f),
           palette_.text, {18, DWRITE_FONT_WEIGHT_BOLD});
      DrawPlaylistCards(home.playlistPanel.left + 14.0f, home.playlistPanel.top + 56.0f,
                        home.playlistPanel.right - 18.0f, home.playlistCardCount, 144.0f);
    }

    if (home.showArtistPanel && IsUsable(home.artistPanel, 300.0f, 160.0f)) {
      DrawArtistPanel(ToD2DRect(home.artistPanel));
    }
  }

  void DrawHero(D2D1_RECT_F rect) {
    if (rect.right - rect.left < 240.0f || rect.bottom - rect.top < 120.0f) {
      return;
    }
    FillRound(rect, 8, D2D1::ColorF(0.64f, 0.80f, 0.88f));
    FillRound(D2D1::RectF(rect.left, std::min(rect.top + 142.0f, rect.bottom - 16.0f), rect.right, rect.bottom), 8,
              D2D1::ColorF(0.93f, 0.82f, 0.55f, 0.42f));
    const float artWidth = std::min((rect.right - rect.left) * 0.52f, 760.0f);
    const float artLeft = std::max(rect.left + 330.0f, rect.right - artWidth - 36.0f);
    FillRound(D2D1::RectF(artLeft, rect.top + 24, rect.right - 36, rect.bottom - 20), 6,
              D2D1::ColorF(0.28f, 0.48f, 0.33f, 0.22f));
    Text(L"今日推荐", D2D1::RectF(rect.left + 28, rect.top + 34, rect.right, rect.top + 60), palette_.text, {14});
    Text(L"Sunshine Acoustic", D2D1::RectF(rect.left + 28, rect.top + 72, rect.right, rect.top + 112),
         D2D1::ColorF(0.02f, 0.02f, 0.02f), {25, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"温暖的旋律，开启活力一天", D2D1::RectF(rect.left + 28, rect.top + 118, rect.right, rect.top + 146),
         palette_.text, {14});
    const float buttonTop = std::min(rect.top + 154.0f, rect.bottom - 54.0f);
    FillRound(D2D1::RectF(rect.left + 28, buttonTop, rect.left + 118, buttonTop + 40.0f), 20, palette_.accent);
    DrawPlayTriangle(D2D1::RectF(rect.left + 40.0f, buttonTop + 10.0f, rect.left + 58.0f, buttonTop + 30.0f),
                     palette_.white);
    Text(L"播放", D2D1::RectF(rect.left + 60, buttonTop + 10.0f, rect.left + 112, buttonTop + 36.0f), palette_.white,
         {14, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Circle(rect.right - 64, rect.bottom - 20, 5, palette_.white);
    Circle(rect.right - 46, rect.bottom - 20, 5, D2D1::ColorF(1, 1, 1, 0.45f));
    Circle(rect.right - 28, rect.bottom - 20, 5, D2D1::ColorF(1, 1, 1, 0.45f));
  }

  void DrawPlaylistCards(float left, float top, float right, int count, float cardHeight = 200.0f) {
    const auto strip = CalculateCardStripLayout(right - left, count, cardHeight);
    if (strip.count <= 0) {
      return;
    }
    const std::array<const wchar_t*, 6> titles = {
        L"清晨轻音乐", L"阳光流行", L"放松时刻", L"旅行日记", L"治愈民谣", L"经典老歌"};
    const std::array<int, 6> nums = {20, 25, 18, 24, 30, 40};
    for (int i = 0; i < strip.count; ++i) {
      const float x = left + i * (strip.itemWidth + strip.gap);
      FillRound(D2D1::RectF(x, top, x + strip.itemWidth, top + strip.itemHeight), 8, palette_.panel);
      StrokeRound(D2D1::RectF(x, top, x + strip.itemWidth, top + strip.itemHeight), 8, palette_.line);
      D2D1_COLOR_F color = D2D1::ColorF(0.48f + i * 0.04f, 0.67f - i * 0.03f, 0.72f - i * 0.02f);
      FillRound(D2D1::RectF(x + 1, top + 1, x + strip.itemWidth - 1, top + strip.imageHeight), 8, color);
      Circle(x + strip.itemWidth - 26, top + strip.imageHeight - 22.0f, 15, D2D1::ColorF(0.03f, 0.03f, 0.025f, 0.72f));
      DrawPlayTriangle(D2D1::RectF(x + strip.itemWidth - 35, top + strip.imageHeight - 34.0f,
                                   x + strip.itemWidth - 17, top + strip.imageHeight - 12.0f),
                       palette_.white);
      Text(titles[static_cast<std::size_t>(i)], D2D1::RectF(x + 12, top + strip.imageHeight + 10.0f,
                                                            x + strip.itemWidth - 12, top + strip.imageHeight + 36.0f),
           palette_.text, {13, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(std::to_wstring(nums[static_cast<std::size_t>(i)]) + L" 首歌曲",
           D2D1::RectF(x + 12, top + strip.imageHeight + 36.0f, x + strip.itemWidth - 12, top + strip.itemHeight - 2.0f),
           palette_.faint, {12});
    }
  }

  void DrawRecentList(D2D1_RECT_F rect) {
    FillRound(rect, 8, palette_.panel);
    StrokeRound(rect, 8, palette_.line);
    const std::array<const wchar_t*, 5> titles = {
        L"Another Day", L"Bloom", L"Your Hand in Mine", L"Holocene", L"River Flows"};
    const std::array<const wchar_t*, 5> artists = {
        L"Mac DeMarco", L"The Paper Kites", L"Explosions In The Sky", L"Bon Iver", L"Yiruma"};
    const std::array<const wchar_t*, 5> times = {L"02:41", L"03:29", L"08:17", L"05:36", L"03:08"};
    float y = rect.top + 18;
    for (std::size_t i = 0; i < titles.size(); ++i) {
      FillRound(D2D1::RectF(rect.left + 22, y - 2, rect.left + 60, y + 36), 5,
                D2D1::ColorF(0.58f + i * 0.05f, 0.52f, 0.36f));
      Text(titles[i], D2D1::RectF(rect.left + 72, y + 6, rect.left + 250, y + 30), palette_.text, {13});
      Text(artists[i], D2D1::RectF(rect.left + 238, y + 6, rect.left + 420, y + 30), palette_.muted, {12});
      Text(times[i], D2D1::RectF(rect.right - 96, y + 6, rect.right - 48, y + 30), palette_.muted, {12});
      Text(L"•••", D2D1::RectF(rect.right - 36, y + 4, rect.right - 8, y + 30), palette_.text, {13});
      if (i + 1 < titles.size()) StrokeLine(rect.left + 70, y + 50, rect.right - 20, y + 50, palette_.line);
      y += 68;
    }
  }

  void DrawArtistPanel(D2D1_RECT_F rect) {
    FillRound(rect, 8, palette_.panel);
    StrokeRound(rect, 8, palette_.line);
    Text(L"艺人推荐", D2D1::RectF(rect.left + 24, rect.top + 20, rect.right, rect.top + 48), palette_.text,
         {18, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"查看全部", D2D1::RectF(rect.right - 80, rect.top + 22, rect.right - 18, rect.top + 48), palette_.accent,
         {13});
    const std::array<const wchar_t*, 4> names = {L"Taylor Swift", L"周杰伦", L"Ed Sheeran", L"Adele"};
    const std::array<D2D1_COLOR_F, 4> colors = {{
        D2D1::ColorF(0.68f, 0.50f, 0.43f),
        D2D1::ColorF(0.09f, 0.14f, 0.20f),
        D2D1::ColorF(0.72f, 0.48f, 0.27f),
        D2D1::ColorF(0.57f, 0.42f, 0.30f),
    }};
    const float gap = (rect.right - rect.left - 96) / 4.0f;
    for (int i = 0; i < 4; ++i) {
      const float cx = rect.left + 64 + i * gap;
      Circle(cx, rect.top + 92, 36, colors[static_cast<std::size_t>(i)]);
      Text(names[static_cast<std::size_t>(i)], D2D1::RectF(cx - 52, rect.top + 132, cx + 52, rect.top + 156),
           palette_.text, {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      Text(L"关注", D2D1::RectF(cx - 34, rect.top + 174, cx + 34, rect.top + 198), palette_.text,
           {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER});
      StrokeRound(D2D1::RectF(cx - 34, rect.top + 170, cx + 34, rect.top + 198), 6, palette_.line);
    }
  }

  void DrawNowPlaying() {
    const float contentLeft = layout_.nowPlaying.tabs.left;
    const float top = layout_.nowPlaying.tabs.top;
    Text(L"正在播放", D2D1::RectF(contentLeft, top, contentLeft + 120, top + 34), palette_.text,
         {17, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(L"歌词", D2D1::RectF(contentLeft + 120, top, contentLeft + 190, top + 34), palette_.text, {16});
    StrokeLine(contentLeft, top + 36, contentLeft + 68, top + 36, palette_.accent, 2.0f);

    DrawAlbumArea(ToD2DRect(layout_.nowPlaying.albumArea));
    DrawLyrics(ToD2DRect(layout_.nowPlaying.lyrics));
    if (layout_.nowPlaying.showQueue) {
      DrawQueue(ToD2DRect(layout_.nowPlaying.queue));
    }
  }

  void DrawSearch() {
    const auto content = layout_.content;
    const float left = content.left + 28.0f;
    const float top = content.top + 28.0f;
    const float right = content.right - 28.0f;
    const float bottom = content.bottom - 24.0f;

    Text(L"搜索结果", D2D1::RectF(left, top, left + 180, top + 36), palette_.text,
         {27, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"关键词 · " + searchView_.keyword, D2D1::RectF(left, top + 42, left + 420, top + 70), palette_.muted,
         {14});
    Text(L"按 1 回首页 · 按 2 到播放页 · 未来这里会接入顶部搜索框输入", D2D1::RectF(right - 520, top + 46, right, top + 72),
         palette_.faint, {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});

    const auto panel = D2D1::RectF(left - 14, top + 92, right, bottom);
    FillRound(panel, 8, palette_.panel);
    StrokeRound(panel, 8, palette_.line);

    Text(L"歌曲", D2D1::RectF(left + 18, top + 116, left + 240, top + 144), palette_.muted, {13});
    Text(L"歌手", D2D1::RectF(left + 380, top + 116, left + 560, top + 144), palette_.muted, {13});
    Text(L"专辑", D2D1::RectF(left + 650, top + 116, left + 850, top + 144), palette_.muted, {13});
    Text(L"时长", D2D1::RectF(right - 100, top + 116, right - 42, top + 144), palette_.muted,
         {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
    StrokeLine(left + 18, top + 150, right - 24, top + 150, palette_.line);

    if (searchView_.state == SearchState::Empty || searchView_.state == SearchState::Error) {
      Text(searchView_.message, D2D1::RectF(left, top + 240, right, top + 280), palette_.muted,
           {18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      return;
    }

    if (searchView_.state == SearchState::Loading) {
      Text(L"正在搜索，请稍候", D2D1::RectF(left, top + 240, right, top + 280), palette_.muted,
           {18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      return;
    }

    constexpr float rowHeight = 58.0f;
    const float listTop = top + 166.0f;
    const auto visibleRows = CalculateVisibleRows(
        searchView_.rows.size(),
        rowHeight,
        0.0f,
        searchScrollOffset_,
        std::max(0.0f, bottom - listTop - 20.0f),
        1);

    for (std::size_t index = visibleRows.first; index < visibleRows.lastExclusive; ++index) {
      const auto& row = searchView_.rows[index];
      const float y = listTop + static_cast<float>(index) * rowHeight - searchScrollOffset_;
      if (index == 0) {
        FillRound(D2D1::RectF(left, y - 6, right - 22, y + 46), 7, D2D1::ColorF(0.90f, 0.92f, 0.93f, 0.55f));
      }
      Text(row.title, D2D1::RectF(left + 18, y + 4, left + 340, y + 30), palette_.text,
           {15, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(row.artist, D2D1::RectF(left + 380, y + 4, left + 610, y + 30), palette_.muted, {13});
      Text(row.album, D2D1::RectF(left + 650, y + 4, right - 140, y + 30), palette_.muted, {13});
      Text(row.duration, D2D1::RectF(right - 100, y + 4, right - 42, y + 30), palette_.muted,
           {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      StrokeLine(left + 18, y + 48, right - 24, y + 48, palette_.line);
    }
  }

  void DrawAlbumArea(D2D1_RECT_F rect) {
    const float availableWidth = std::max(260.0f, rect.right - rect.left);
    const float coverSize = std::clamp(availableWidth - 28.0f, 260.0f, 400.0f);
    const bool showVinyl = availableWidth >= 390.0f;
    FillRound(D2D1::RectF(rect.left, rect.top, rect.left + coverSize, rect.top + coverSize), 8,
              D2D1::ColorF(0.20f, 0.15f, 0.12f));
    if (showVinyl) {
      Circle(rect.left + coverSize + 5.0f, rect.top + coverSize * 0.5f, coverSize * 0.29f,
             D2D1::ColorF(0.04f, 0.05f, 0.055f, 0.88f));
      Circle(rect.left + coverSize + 5.0f, rect.top + coverSize * 0.5f, coverSize * 0.11f,
             D2D1::ColorF(0.12f, 0.13f, 0.13f));
    }
    Text(L"叶惠美", D2D1::RectF(rect.left + coverSize - 105.0f, rect.top + 32, rect.left + coverSize - 18.0f, rect.top + 120),
         D2D1::ColorF(0.82f, 0.62f, 0.30f), {20, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(L"周杰伦", D2D1::RectF(rect.left + coverSize * 0.30f, rect.top + coverSize - 62.0f,
                               rect.left + coverSize - 80.0f, rect.top + coverSize - 10.0f),
         D2D1::ColorF(0.82f, 0.62f, 0.30f), {coverSize < 330.0f ? 24.0f : 30.0f, DWRITE_FONT_WEIGHT_BOLD});

    const float detailTop = rect.top + coverSize + 32.0f;
    Text(playerView_.title, D2D1::RectF(rect.left, detailTop, rect.left + availableWidth - 24.0f, detailTop + 38.0f), palette_.text,
         {coverSize < 330.0f ? 25.0f : 31.0f, DWRITE_FONT_WEIGHT_BOLD});
    Text(playerView_.artist, D2D1::RectF(rect.left, detailTop + 56.0f, rect.left + availableWidth - 24.0f, detailTop + 86.0f), palette_.text, {20});
    Text(L"专辑 · " + playerView_.album, D2D1::RectF(rect.left, detailTop + 96.0f, rect.left + availableWidth - 24.0f, detailTop + 122.0f), palette_.muted,
         {14});
    DrawButton(D2D1::RectF(rect.left, detailTop + 136.0f, rect.left + 92.0f, detailTop + 168.0f), L"SQ 无损音质", false);
    DrawButton(D2D1::RectF(rect.left + 112.0f, detailTop + 136.0f, rect.left + 178.0f, detailTop + 168.0f), L"已关注", false);
    Text(L"♥", D2D1::RectF(rect.left + availableWidth - 78.0f, detailTop + 2.0f, rect.left + availableWidth - 46.0f, detailTop + 38.0f),
         palette_.accent, {26});
    Text(L"•••", D2D1::RectF(rect.left + availableWidth - 36.0f, detailTop + 8.0f, rect.left + availableWidth, detailTop + 34.0f),
         palette_.muted, {18});
    DrawProgress(rect.left, detailTop + 214.0f, rect.left + std::min(405.0f, availableWidth - 24.0f), 0.48f);
    Text(L"01:42", D2D1::RectF(rect.left, detailTop + 232.0f, rect.left + 60.0f, detailTop + 256.0f), palette_.muted, {12});
    Text(playerView_.duration, D2D1::RectF(rect.left + availableWidth - 72.0f, detailTop + 232.0f, rect.left + availableWidth - 16.0f, detailTop + 256.0f), palette_.muted, {12});
  }

  void DrawLyrics(D2D1_RECT_F rect) {
    Text(playerView_.title + L" - " + playerView_.artist, D2D1::RectF(rect.left, rect.top + 10, rect.right, rect.top + 42), palette_.text,
         {20, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER});
    Text(L"词：BottleMusic    曲：Native Preview", D2D1::RectF(rect.left, rect.top + 50, rect.right, rect.top + 78), palette_.muted,
         {14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    if (lyricView_.state == LyricUiState::Empty) {
      Text(lyricView_.message, D2D1::RectF(rect.left, rect.top + 220, rect.right, rect.top + 260), palette_.muted,
           {18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      return;
    }

    float y = rect.top + 100;
    for (std::size_t i = 0; i < lyricView_.lines.size(); ++i) {
      const bool active = lyricView_.lines[i].active;
      Text(lyricView_.lines[i].text, D2D1::RectF(rect.left, y, rect.right, y + 38),
           active ? palette_.accent : D2D1::ColorF(0.38f, 0.38f, 0.36f),
           {active ? 24.0f : 19.0f,
            active ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER});
      y += active ? 48.0f : 42.0f;
    }
    Text(L"歌词设置      翻译      歌词报错", D2D1::RectF(rect.left, rect.bottom - 72, rect.right, rect.bottom - 40),
         palette_.muted, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
  }

  void DrawQueue(D2D1_RECT_F rect) {
    FillRound(rect, 8, palette_.panel);
    StrokeRound(rect, 8, palette_.line);
    Text(L"播放队列", D2D1::RectF(rect.left + 24, rect.top + 24, rect.left + 130, rect.top + 54), palette_.text,
         {19, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"历史记录", D2D1::RectF(rect.left + 136, rect.top + 26, rect.left + 230, rect.top + 54), palette_.muted, {15});
    StrokeLine(rect.left + 24, rect.top + 60, rect.left + 94, rect.top + 60, palette_.accent, 2);
    StrokeLine(rect.left + 24, rect.top + 60, rect.right - 24, rect.top + 60, palette_.line);
    Text(L"当前播放   ⠂", D2D1::RectF(rect.left + 24, rect.top + 86, rect.left + 180, rect.top + 116), palette_.muted,
         {13});
    Text(L"清空    •••", D2D1::RectF(rect.right - 112, rect.top + 86, rect.right - 20, rect.top + 116), palette_.muted,
         {13});

    const std::array<const wchar_t*, 8> songs = {
        L"晴天", L"七里香", L"一路向北", L"稻香", L"夜曲", L"不能说的秘密", L"简单爱", L"轨迹"};
    const std::array<const wchar_t*, 8> times = {L"04:29", L"04:57", L"04:55", L"03:43", L"03:48", L"04:56", L"04:30", L"04:41"};
    constexpr float rowHeight = 68.0f;
    const auto visibleRows = CalculateVisibleRows(
        songs.size(),
        rowHeight,
        0.0f,
        queueScrollOffset_,
        std::max(0.0f, rect.bottom - rect.top - 190.0f),
        1);
    for (std::size_t i = visibleRows.first; i < visibleRows.lastExclusive; ++i) {
      const float y = rect.top + 130.0f + static_cast<float>(i) * rowHeight - queueScrollOffset_;
      if (i == 0) FillRound(D2D1::RectF(rect.left + 12, y - 10, rect.right - 12, y + 58), 7, palette_.panelStrong);
      Text(i == 0 ? L"▮▮" : std::to_wstring(i + 1), D2D1::RectF(rect.left + 26, y + 8, rect.left + 58, y + 34),
           i == 0 ? palette_.accent : palette_.text, {13});
      Text(songs[i], D2D1::RectF(rect.left + 70, y, rect.right - 90, y + 26), palette_.text,
           {14, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(L"周杰伦", D2D1::RectF(rect.left + 70, y + 26, rect.right - 90, y + 50), palette_.muted, {12});
      Text(times[i], D2D1::RectF(rect.right - 76, y + 8, rect.right - 20, y + 36), palette_.muted, {12});
      if (i + 1 < songs.size()) StrokeLine(rect.left + 70, y + 62, rect.right - 24, y + 62, palette_.line);
    }
    Text(L"8 首歌曲 · 33 分钟", D2D1::RectF(rect.left + 24, rect.bottom - 42, rect.left + 180, rect.bottom - 16),
         palette_.muted, {13});
    Text(L"保存为歌单", D2D1::RectF(rect.right - 100, rect.bottom - 42, rect.right - 20, rect.bottom - 16), palette_.accent,
         {13});
  }

  void DrawButton(D2D1_RECT_F rect, const std::wstring& label, bool primary) {
    FillRound(rect, 8, primary ? palette_.accent : D2D1::ColorF(0.96f, 0.94f, 0.90f, 0.75f));
    StrokeRound(rect, 8, primary ? palette_.accent : palette_.line);
    Text(label, D2D1::RectF(rect.left, rect.top + 4, rect.right, rect.bottom), primary ? palette_.white : palette_.text,
         {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
  }

  void DrawProgress(float left, float y, float right, float progress) {
    if (right <= left + 12.0f) {
      return;
    }
    StrokeLine(left, y, right, y, D2D1::ColorF(0.64f, 0.64f, 0.60f, 0.32f), 3.0f);
    StrokeLine(left, y, left + (right - left) * progress, y, palette_.accent, 3.0f);
    Circle(left + (right - left) * progress, y, 6, palette_.accent);
  }

  void DrawPlayerBar() {
    const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
    const float top = bar.bar.top;
    FillRound(ToD2DRect(bar.bar), 8, palette_.panel);
    StrokeRound(ToD2DRect(bar.bar), 8, palette_.line);
    FillRound(ToD2DRect(bar.albumArt), 6,
              surface_ == Surface::Home ? D2D1::ColorF(0.60f, 0.74f, 0.70f) : D2D1::ColorF(0.20f, 0.15f, 0.12f));
    Text(surface_ == Surface::Home && playerView_.state == PlayerUiState::Idle ? L"Sunshine Acoustic" : playerView_.title,
         ToD2DRect(bar.title), palette_.text, {15, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(surface_ == Surface::Home && playerView_.state == PlayerUiState::Idle ? L"Leavv" : playerView_.artist,
         ToD2DRect(bar.artist), palette_.muted, {13});
    Text(L"♥", ToD2DRect(bar.favorite), palette_.accent, {22});

    if (bar.showSecondaryControls) {
      Text(L"♢", ToD2DRect(bar.shuffle), palette_.muted, {21, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    }
    DrawPlayTriangle(ToD2DRect(bar.previous), palette_.muted, false);
    Circle((bar.playPause.left + bar.playPause.right) * 0.5f, (bar.playPause.top + bar.playPause.bottom) * 0.5f, 24,
           palette_.accentDark);
    if (playerView_.state == PlayerUiState::Playing) {
      DrawPauseIcon(ToD2DRect(bar.playPause), palette_.white);
    } else {
      DrawPlayTriangle(ToD2DRect(bar.playPause), palette_.white, true);
    }
    DrawPlayTriangle(ToD2DRect(bar.next), palette_.muted, true);
    if (bar.showSecondaryControls) {
      Text(L"↻", ToD2DRect(bar.repeat), palette_.muted, {21, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    }
    Text(playerView_.state == PlayerUiState::Idle ? L"01:42" : playerView_.current,
         ToD2DRect(bar.currentTime), palette_.muted, {12});
    DrawProgress(bar.progress.left, bar.progress.top, bar.progress.right,
                 playerView_.state == PlayerUiState::Idle ? 0.55f : static_cast<float>(playerView_.progress));
    Text(surface_ == Surface::Home && playerView_.state == PlayerUiState::Idle ? L"03:54" : playerView_.duration,
         ToD2DRect(bar.duration), palette_.muted, {12});

    if (bar.showVolume) {
      Text(L"🔊", ToD2DRect(bar.volumeIcon), palette_.muted, {17});
      DrawProgress(bar.volume.left, bar.volume.top, bar.volume.right, 0.48f);
    }
    Text(L"≡", ToD2DRect(bar.queue), palette_.muted, {22, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    DrawButton(ToD2DRect(bar.lyric), L"词", false);
  }

  HWND hwnd_ = nullptr;
  ID2D1Factory* d2dFactory_ = nullptr;
  ID2D1HwndRenderTarget* renderTarget_ = nullptr;
  IDWriteFactory* writeFactory_ = nullptr;
  Palette palette_;
  Surface surface_ = Surface::Home;
  std::unique_ptr<core::IBackendFacade> backend_;
  std::future<std::unique_ptr<core::IBackendFacade>> backendFuture_;
  std::future<core::DeviceInfo> deviceFuture_;
  core::DeviceInfo device_;
  std::wstring deviceStatus_ = L"设备未初始化";
  float clientWidth_ = 1600.0f;
  float clientHeight_ = 1060.0f;
  MelodyLayout layout_ = CalculateMelodyLayout(1600.0f, 1060.0f);
  float queueScrollOffset_ = 0.0f;
  SearchViewModel searchView_;
  std::future<nlohmann::json> searchFuture_;
  std::string searchKeyword_;
  float searchScrollOffset_ = 0.0f;
  playback::PlaybackController playback_;
  bool playbackInitialized_ = false;
  PlaybackViewModel playerView_;
  SearchResultRow pendingPlaybackRow_;
  std::future<nlohmann::json> songUrlFuture_;
  core::LyricDocument lyricDocument_;
  LyricViewModel lyricView_;
  std::int64_t playbackPositionMs_ = 102000;
};

}  // namespace

int Run(HINSTANCE instance, int showCommand) {
  EnableDpiAwareness();
  MainWindow window;
  if (!window.Create(instance, showCommand)) return 1;

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

}  // namespace echo::win32_app

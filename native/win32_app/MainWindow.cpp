#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <cmath>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "echo/core/BackendFacade.h"
#include "echo/core/HttpClient.h"
#include "echo/core/LyricParser.h"
#include "echo/image/ImageCache.h"
#include "echo/image/ImageLoader.h"
#include "echo/playback/PlaybackController.h"
#include "echo/storage/AppPaths.h"
#include "echo/win32_app/ImageSlot.h"
#include "echo/win32_app/Layout.h"
#include "echo/win32_app/LyricViewModel.h"
#include "echo/win32_app/Navigation.h"
#include "echo/win32_app/PlaybackQueue.h"
#include "echo/win32_app/PlaybackViewModel.h"
#include "echo/win32_app/GlassPanel.h"
#include "echo/win32_app/PaperTexture.h"
#include "echo/win32_app/Painter.h"
#include "echo/win32_app/RenderPipeline.h"
#include "echo/win32_app/SearchInput.h"
#include "echo/win32_app/SearchViewModel.h"
#include "echo/win32_app/Theme.h"

namespace echo::win32_app {
namespace {

template <typename T>
void SafeRelease(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

// Palette 已迁移到 echo/win32_app/Theme.h；MainWindow 通过 MakeNewsprintPalette() 初始化 palette_。
// 字段名（bg/panel/line/text/...）保持与现有 ~160 处 palette_.X 调用兼容；色值已换为 Newsprint。

struct TextStyle {
  float size = 14.0f;
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
  DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
  DWRITE_WORD_WRAPPING wrapping = DWRITE_WORD_WRAPPING_WRAP;
  DWRITE_TRIMMING_GRANULARITY trimming = DWRITE_TRIMMING_GRANULARITY_NONE;
};

std::wstring ToWideAscii(const std::string& value) {
  return std::wstring(value.begin(), value.end());
}

std::wstring DeviceTail(const std::string& value) {
  if (value.empty()) return L"";
  const auto start = value.size() > 6 ? value.size() - 6 : 0;
  return ToWideAscii(value.substr(start));
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8,
      0,
      value.data(),
      static_cast<int>(value.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(
      CP_UTF8,
      0,
      value.data(),
      static_cast<int>(value.size()),
      result.data(),
      size,
      nullptr,
      nullptr);
  return result;
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

std::string JsonString(const nlohmann::json& value, const char* key) {
  const auto found = value.find(key);
  if (found == value.end()) {
    return {};
  }
  if (found->is_string()) {
    return found->get<std::string>();
  }
  if (found->is_number_integer()) {
    return std::to_string(found->get<std::int64_t>());
  }
  if (found->is_number_unsigned()) {
    return std::to_string(found->get<std::uint64_t>());
  }
  return {};
}

std::pair<std::string, std::string> FirstLyricCandidate(const nlohmann::json& response) {
  const nlohmann::json* candidates = nullptr;
  if (response.contains("candidates") && response["candidates"].is_array()) {
    candidates = &response["candidates"];
  } else if (response.contains("data") && response["data"].is_object()) {
    const auto& data = response["data"];
    if (data.contains("candidates") && data["candidates"].is_array()) {
      candidates = &data["candidates"];
    } else if (data.contains("info") && data["info"].is_array()) {
      candidates = &data["info"];
    }
  }

  if (!candidates || candidates->empty() || !(*candidates)[0].is_object()) {
    return {};
  }

  const auto& first = (*candidates)[0];
  return {JsonString(first, "id"), JsonString(first, "accesskey")};
}

void EnableDpiAwareness() {
  using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  auto* user32 = GetModuleHandleW(L"user32.dll");
  auto* setAwareness = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
      user32 ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr);
  if (setAwareness && setAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    return;
  }

  using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
  auto* shcore = LoadLibraryW(L"shcore.dll");
  auto* setProcessDpiAwareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
      shcore ? GetProcAddress(shcore, "SetProcessDpiAwareness") : nullptr);
  if (setProcessDpiAwareness && SUCCEEDED(setProcessDpiAwareness(2))) {
    if (shcore) FreeLibrary(shcore);
    return;
  }
  if (shcore) FreeLibrary(shcore);

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
    ApplyQueueTrack(queueState_.Current(), PlayerUiState::Idle);
  }

  ~MainWindow() {
    DiscardDeviceResources();
    SafeRelease(dashStrokeStyle_);
    SafeRelease(transientBrush_);
    SafeRelease(writeFactory_);
    // d2dFactory_ 是 renderPipeline_ 的非拥有指针；renderPipeline_ 析构时统一释放。
    d2dFactory_ = nullptr;
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

    // Newsprint 标题栏融合：把系统 caption 染成 Paper 色，文字 Ink 色，边框 Rule 色。
    // DWMWA_CAPTION_COLOR / TEXT_COLOR / BORDER_COLOR 需 Windows 11 22000+；旧系统会被忽略，无副作用。
    {
      COLORREF caption = RGB(0xf1, 0xea, 0xd8);  // Paper
      COLORREF inkText = RGB(0x22, 0x1b, 0x12);  // Ink
      COLORREF border  = RGB(0xd8, 0xcd, 0xb1);  // PaperEdge（Rule 是半透明，DWM 需要不透明色）
      DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
      DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR,    &inkText, sizeof(inkText));
      DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR,  &border,  sizeof(border));
    }

    // 隐藏标题栏图标和文字（窗口拖动仍可工作）。
    // WS_EX_DLGMODALFRAME 移除系统图标占位；SendMessage WM_SETICON 清掉应用图标。
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE,
                      GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_DLGMODALFRAME);
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, 0);
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,   0);
    SetWindowTextW(hwnd_, L"");  // 清空标题栏文字
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    ShowWindow(hwnd_, showCommand);
    FitToWorkArea();
    UpdateWindow(hwnd_);
    return true;
  }

 private:
  enum class NowPlayingTab { Overview, Lyrics };
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
        RequestAppIcon();
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
        RECT minClient{
            0,
            0,
            900,
            640};
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
        if (searchInput_.IsFocused()) {
          if (wParam == VK_ESCAPE) {
            searchInput_.Blur();
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
          return 0;
        }
        if (wParam == '1') NavigateTo(PageId::Home);
        if (wParam == '2') NavigateTo(PageId::NowPlaying);
        if (wParam == 'L') NavigateTo(PageId::NowPlaying);
        if (wParam == 'S') BeginSearch(L"晴天", "晴天");
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      case WM_CHAR:
        HandleCharacter(static_cast<wchar_t>(wParam));
        return 0;
      case WM_LBUTTONUP:
        HandleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
      case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
          HandleDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
      case WM_MOUSEWHEEL:
        HandleMouseWheel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), GET_WHEEL_DELTA_WPARAM(wParam));
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
    // 在 WM_CREATE 阶段先创建工厂与设备链路（无 swap chain），
    // swap chain 与后台缓冲在首次 WM_PAINT 经 CreateDeviceResources -> Initialize(hwnd) 路径完成。
    if (!renderPipeline_.InitializeHeadless()) {
      return false;
    }
    d2dFactory_ = renderPipeline_.factory();
    if (!SUCCEEDED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&writeFactory_))))
      return false;
    // Painter TextFormat 缓存（设备无关，一次创建后跨设备重建复用）。
    painter_.InitializeFonts(writeFactory_);
    // 虚线笔刷样式（factory 级别，跨设备复用）。
    if (d2dFactory_) {
      D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
      props.dashStyle = D2D1_DASH_STYLE_DASH;
      d2dFactory_->CreateStrokeStyle(props, nullptr, 0, &dashStrokeStyle_);
    }
    return true;
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

  float DevicePxToDip(float value) const {
    return value * 96.0f / WindowDpi();
  }

  float WindowDpi() const {
    if (!hwnd_) {
      return 96.0f;
    }
    const auto dpi = GetDpiForWindow(hwnd_);
    return dpi == 0 ? 96.0f : static_cast<float>(dpi);
  }

  D2D1_SIZE_F CurrentClientDipSize() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return D2D1::SizeF(
        DevicePxToDip(static_cast<float>(rc.right - rc.left)),
        DevicePxToDip(static_cast<float>(rc.bottom - rc.top)));
  }

  void BeginSearch(std::wstring displayKeyword, std::string keyword) {
    NavigateTo(PageId::Search);
    searchInput_.SetText(displayKeyword);
    searchInput_.Blur();
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

    const auto requestedRow = searchView_.rows[rowIndex];
    playbackCandidates_ = RankQueuePlaybackCandidates(requestedRow, searchView_);
    if (playbackCandidates_.empty()) {
      playbackCandidates_.push_back(requestedRow);
    }
    playbackCandidateIndex_ = 0;
    pendingPlaybackRow_ = playbackCandidates_[playbackCandidateIndex_];
    playerView_ = PlaybackViewModel{};
    playerView_.state = PlayerUiState::Resolving;
    playerView_.title = requestedRow.title;
    playerView_.artist = requestedRow.artist;
    playerView_.album = requestedRow.album;
    playerView_.duration = requestedRow.duration;
    playerView_.current = L"00:00";
    lyricDocument_ = {};
    lyricView_ = {};
    lyricView_.message = L"正在解析播放地址";

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
      lyricView_.message = L"歌词启动失败";
    }

    NavigateTo(PageId::NowPlaying);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void BeginResolveAndPlay(const SearchResultRow& row) {
    searchView_.state = SearchState::Ready;
    searchView_.rows = {row};
    BeginResolveAndPlay(0);
  }

  void BeginResolveAndPlayQueueTrack(const QueueTrack& track) {
    auto row = BuildSearchRowFromQueueTrack(track);
    playerView_ = PlaybackViewModel{};
    playerView_.state = PlayerUiState::Resolving;
    playerView_.title = row.title;
    playerView_.artist = row.artist;
    playerView_.album = row.album;
    playerView_.duration = row.duration;
    playerView_.current = L"00:00";
    lyricDocument_ = {};
    lyricView_ = {};
    lyricView_.message = L"正在匹配歌曲";
    nowPlayingTab_ = NowPlayingTab::Overview;
    NavigateTo(PageId::NowPlaying);

    if (!row.hash.empty()) {
      BeginResolveAndPlay(row);
      return;
    }

    try {
      if (!backend_) {
        StartBackend();
        playerView_.state = PlayerUiState::Error;
        playerView_.error = L"后端初始化中，请稍后再试";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }

      pendingQueuePlaybackRow_ = std::move(row);
      const auto query = BuildQueueTrackSearchText(track);
      queuePlaybackSearchKeyword_ = WideToUtf8(query);
      queuePlaybackSearchFuture_ = backend_->SearchSongs(queuePlaybackSearchKeyword_, 1, 30);
      SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    } catch (...) {
      playerView_.state = PlayerUiState::Error;
      playerView_.error = L"播放匹配启动失败";
      lyricView_.message = L"歌词启动失败";
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void PollBackend() {
    bool hasPendingWork = false;

    if (appIconFuture_.valid()) {
      if (appIconFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto payload = appIconFuture_.get();
        if (payload.bgra.empty()) {
          appIconSlot_.Fail("app-icon");
        } else {
          appIconSlot_.Complete("app-icon", std::move(payload));
        }
        SafeRelease(appIconBitmap_);
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    for (auto& [key, state] : artworkStates_) {
      if (!state.future.valid()) {
        continue;
      }
      if (state.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto payload = state.future.get();
        if (payload.bgra.empty()) {
          state.slot.Fail(key);
        } else {
          state.slot.Complete(key, std::move(payload));
        }
        SafeRelease(state.bitmap);
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (backendFuture_.valid()) {
      if (backendFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          backend_ = backendFuture_.get();
          if (backend_) {
            deviceFuture_ = backend_->EnsureDeviceReady();
            settingsFuture_ = backend_->LoadSettings();
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

    if (loginQrFuture_.valid()) {
      if (loginQrFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto result = loginQrFuture_.get();
          if (result.contains("data") && result["data"].is_object()) {
            loginQrKey_ = result["data"].value("qrcode", "");
            loginQrUrl_ = result["data"].value("qrcodeurl", "");
            if (loginQrUrl_.empty()) {
              loginQrUrl_ = result["data"].value("url", "");
            }
          }
        } catch (...) {}
        
        if (loginQrKey_.empty()) {
          loginQrKey_ = "error"; // Prevent infinite retry loop
        }
        
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (!loginQrKey_.empty() && loginQrKey_ != "error" && !loginPollFuture_.valid() && backend_) {
      loginPollFuture_ = backend_->PollQrLogin(loginQrKey_);
      hasPendingWork = true;
    }

    if (loginPollFuture_.valid()) {
      if (loginPollFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto result = loginPollFuture_.get();
          if (result.contains("data") && result["data"].is_object()) {
            const int status = result["data"].value("status", 0);
            if (status == 4) {
              // Success
              loginQrKey_.clear();
              loginQrUrl_.clear();
              isRequestingQr_ = false;
              NavigateTo(PageId::Home);
            } else if (status == 2 || status == 3) {
              // Waiting or scanning, do nothing, just allow polling again
            } else if (status == 5 || status == 0) {
              // Expired or error, reset and request new QR
              loginQrKey_.clear();
              loginQrUrl_.clear();
              isRequestingQr_ = false;
            }
          }
        } catch (...) {
          // Error, reset
          loginQrKey_.clear();
          loginQrUrl_.clear();
          isRequestingQr_ = false;
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

    if (settingsFuture_.valid()) {
      if (settingsFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto settings = settingsFuture_.get();
          volume_ = static_cast<float>(std::clamp(settings.volume, 0.0, 1.0));
          playback_.SetVolume(volume_);
        } catch (...) {
          deviceStatus_ = L"设置加载失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (settingsSaveFuture_.valid()) {
      if (settingsSaveFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          settingsSaveFuture_.get();
        } catch (...) {
          deviceStatus_ = L"设置保存失败";
        }
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

    if (queuePlaybackSearchFuture_.valid()) {
      if (queuePlaybackSearchFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto lookup = BuildSearchViewModel(queuePlaybackSearchKeyword_, queuePlaybackSearchFuture_.get());
          playbackCandidates_ = RankQueuePlaybackCandidates(pendingQueuePlaybackRow_, lookup);
          if (playbackCandidates_.empty()) {
            playerView_.state = PlayerUiState::Error;
            playerView_.error = L"没有找到可播放的歌曲";
            lyricView_.message = L"暂无歌词";
          } else {
            searchView_.state = SearchState::Ready;
            searchView_.rows = playbackCandidates_;
            BeginResolveAndPlay(0);
            hasPendingWork = true;
          }
        } catch (...) {
          playerView_.state = PlayerUiState::Error;
          playerView_.error = L"播放匹配失败";
          lyricView_.message = L"歌词启动失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (songUrlFuture_.valid()) {
      if (songUrlFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto response = songUrlFuture_.get();
          playerView_ = BuildPlaybackViewModel(pendingPlaybackRow_, response);
          if (playerView_.state == PlayerUiState::Ready && EnsurePlaybackReady() &&
              playback_.PlayUrl(playerView_.sourceUrl)) {
            playerView_.state = PlayerUiState::Playing;
            playerView_.current = L"00:00";
            playerView_.progress = 0.0;
            playbackPositionMs_ = 0;
            lyricView_ = {};
            lyricView_.message = L"正在获取歌词";
            if (backend_) {
              lyricSearchFuture_ = backend_->SearchLyrics(pendingPlaybackRow_.hash);
            }
            hasPendingWork = true;
          } else if (playerView_.state == PlayerUiState::Ready) {
            if (TryResolveNextPlaybackCandidate(L"播放地址打开失败")) {
              hasPendingWork = true;
            } else {
              playerView_.state = PlayerUiState::Error;
              playerView_.error = L"播放地址打开失败";
            }
          } else if (playerView_.state == PlayerUiState::Error) {
            if (TryResolveNextPlaybackCandidate(playerView_.error)) {
              hasPendingWork = true;
            }
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

    if (lyricSearchFuture_.valid()) {
      if (lyricSearchFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          const auto lyricSearch = lyricSearchFuture_.get();
          const auto [id, accessKey] = FirstLyricCandidate(lyricSearch);
          if (!id.empty() && !accessKey.empty() && backend_) {
            lyricDetailFuture_ = backend_->GetLyricDetail(id, accessKey);
            hasPendingWork = true;
          } else {
            lyricView_ = {};
            lyricView_.message = L"暂无歌词";
          }
        } catch (...) {
          lyricView_ = {};
          lyricView_.message = L"歌词搜索失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (lyricDetailFuture_.valid()) {
      if (lyricDetailFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
          lyricDocument_ = BuildLyricDocumentFromDetail(lyricDetailFuture_.get());
          if (lyricDocument_.lines.empty()) {
            lyricView_ = {};
            lyricView_.message = L"暂无歌词";
          } else {
            ApplyPlaybackProgress(playerView_, lyricView_, lyricDocument_, playerView_.progress);
          }
        } catch (...) {
          lyricView_ = {};
          lyricView_.message = L"歌词加载失败";
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else {
        hasPendingWork = true;
      }
    }

    if (playbackInitialized_) {
      const auto playbackState = playback_.GetState();
      if (playbackState.kind == core::PlaybackStateKind::Opening ||
          playbackState.kind == core::PlaybackStateKind::Playing ||
          playbackState.kind == core::PlaybackStateKind::Paused) {
        if (playbackState.kind == core::PlaybackStateKind::Paused) {
          playerView_.state = PlayerUiState::Paused;
        } else if (playbackState.kind == core::PlaybackStateKind::Playing) {
          playerView_.state = PlayerUiState::Playing;
        }
        ApplyPlaybackStateSnapshot(playerView_, lyricView_, lyricDocument_, playbackState);
        playbackPositionMs_ = static_cast<std::int64_t>(std::llround(playbackState.currentSeconds * 1000.0));
        InvalidateRect(hwnd_, nullptr, FALSE);
        hasPendingWork = true;
      } else if (playbackState.kind == core::PlaybackStateKind::Stopped) {
        playerView_.state = PlayerUiState::Paused;
        ApplyPlaybackStateSnapshot(playerView_, lyricView_, lyricDocument_, playbackState);
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (playbackState.kind == core::PlaybackStateKind::Failed && !playbackState.error.empty()) {
        const auto playbackError = ToWideAscii(playbackState.error);
        if (TryResolveNextPlaybackCandidate(playbackError)) {
          hasPendingWork = true;
        } else {
          playerView_.state = PlayerUiState::Error;
          playerView_.error = playbackError;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
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
    const UINT width = static_cast<UINT>(rc.right - rc.left);
    const UINT height = static_cast<UINT>(rc.bottom - rc.top);
    if (!renderPipeline_.Initialize(hwnd_, width, height)) {
      return E_FAIL;
    }
    renderTarget_ = renderPipeline_.device_context();
    d2dFactory_ = renderPipeline_.factory();
    const auto dpi = WindowDpi();
    renderPipeline_.SetDpi(dpi);
    // 抗锯齿 / ClearType 模式由 RenderPipeline 在 DeviceContext 创建后默认设置；
    // 这里冗余设置以保留旧路径语义，便于后续切片再单独抽出。
    renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // Painter 笔刷（设备相关；设备丢失后 DetachContext 释放，重建时重新 Attach）。
    painter_.AttachContext(renderTarget_);

    // List 19：玻璃面板 + 纸纹。GlassPanel 需要 backbuffer 尺寸；
    // 失败不致命（player bar 退回普通纯色），仅记 OutputDebugString。
    paperTex_.Initialize(renderTarget_);
    if (!glass_.Initialize(renderTarget_, width, height)) {
      OutputDebugStringW(L"[List19] GlassPanel::Initialize failed; player bar will use solid fallback.\n");
    }
    return S_OK;
  }

  void DiscardDeviceResources() {
    SafeRelease(appIconBitmap_);
    for (auto& [key, state] : artworkStates_) {
      (void)key;
      SafeRelease(state.bitmap);
    }
    // Painter 笔刷先于 DeviceContext 释放（brushes hold device ref）。
    painter_.DetachContext();
    // List 19 资源先于 DeviceContext 释放（持有 effect / bitmap / brush 引用）。
    glass_.OnDeviceLost();
    paperTex_.OnDeviceLost();
    SafeRelease(transientBrush_);
    // renderTarget_ 是 renderPipeline_ 的非拥有指针；Shutdown 释放真正的 DeviceContext。
    // d2dFactory_ 同理（factory 跨设备保留），但 Shutdown 不释放工厂，仅释放设备链路。
    renderTarget_ = nullptr;
    renderPipeline_.Shutdown();
    d2dFactory_ = nullptr;
  }

  void ResizeRenderTarget() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT pxWidth = static_cast<UINT>(rc.right - rc.left);
    const UINT pxHeight = static_cast<UINT>(rc.bottom - rc.top);
    const auto size = D2D1::SizeF(
        DevicePxToDip(static_cast<float>(pxWidth)),
        DevicePxToDip(static_cast<float>(pxHeight)));
    clientWidth_ = size.width;
    clientHeight_ = size.height;
    layout_ = CalculateMelodyLayout(clientWidth_, clientHeight_);

    if (renderTarget_) {
      const auto dpi = WindowDpi();
      renderPipeline_.SetDpi(dpi);
      // 设备像素 swap chain resize；DPI 缩放仅影响 D2D 坐标变换，不改变 swap chain 尺寸。
      const HRESULT hr = renderPipeline_.Resize(pxWidth, pxHeight);
      if (RenderPipeline::IsDeviceLossHResult(hr)) {
        DiscardDeviceResources();
      } else {
        // List 19：swap chain 尺寸变更 → GlassPanel 内部 bitmap 需重建并标记 blur 脏。
        glass_.EnsureSceneSize(pxWidth, pxHeight);
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void RequestAppIcon() {
    const auto decision = appIconSlot_.Request("app-icon");
    if (!decision.shouldStartLoad) {
      return;
    }

    const auto path = FindAssetPath(L"assets/icons/icon.png");
    if (path.empty()) {
      appIconSlot_.Fail(decision.key);
      return;
    }

    appIconFuture_ = std::async(std::launch::async, [path] {
      const auto decoded = image::WicImageDecoder{}.DecodeFile(path);
      if (decoded.placeholder || decoded.bgra.empty()) {
        return ImageSlotPayload{};
      }
      return ImageSlotPayload{decoded.width, decoded.height, decoded.bgra};
    });
    SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
  }

  void HandleDrag(int x, int y) {
    const float dipX = DevicePxToDip(static_cast<float>(x));
    const float dipY = DevicePxToDip(static_cast<float>(y));
    const auto playerAction =
        HitTestPlayerBar(CalculatePlayerBarLayout(clientWidth_, clientHeight_), dipX, dipY);
    
    if (playerAction == PlayerBarAction::Seek) {
      const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
      ApplyPlaybackProgress(
          playerView_,
          lyricView_,
          lyricDocument_,
          TrackValueFromPoint(bar.progress, dipX));
      playback_.Seek(DurationToSeconds(playerView_.duration) * playerView_.progress);
      InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (playerAction == PlayerBarAction::SetVolume) {
      const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
      volume_ = TrackValueFromPoint(bar.volume, dipX);
      playback_.SetVolume(volume_);
      SaveSettingsSnapshot();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void HandleClick(int x, int y) {
    const float dipX = DevicePxToDip(static_cast<float>(x));
    const float dipY = DevicePxToDip(static_cast<float>(y));
    const auto headerAction = HitTestHeader(CalculateHeaderControlsLayout(clientWidth_, layout_.sidebar.right), dipX, dipY);
    if (headerAction == HeaderAction::Back) {
      if (navigation_.GoBack()) {
        surface_ = navigation_.Current();
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (headerAction == HeaderAction::Forward) {
      if (navigation_.GoForward()) {
        surface_ = navigation_.Current();
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (headerAction == HeaderAction::Search) {
      searchInput_.Focus();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (headerAction == HeaderAction::Avatar) {
      NavigateTo(PageId::Login);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }

    const auto playerAction =
        HitTestPlayerBar(CalculatePlayerBarLayout(clientWidth_, clientHeight_), dipX, dipY);
    if (playerAction == PlayerBarAction::OpenLyrics) {
      nowPlayingTab_ = NowPlayingTab::Lyrics;
      NavigateTo(PageId::NowPlaying);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::OpenNowPlaying) {
      nowPlayingTab_ = NowPlayingTab::Overview;
      NavigateTo(PageId::NowPlaying);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::TogglePlay) {
      if (playerView_.state == PlayerUiState::Playing) {
        playback_.Pause();
        playerView_.state = PlayerUiState::Paused;
      } else if (playerView_.state == PlayerUiState::Idle) {
        if (const auto* track = queueState_.Current()) {
          BeginResolveAndPlayQueueTrack(*track);
        }
      } else if (playerView_.state == PlayerUiState::Ready || playerView_.state == PlayerUiState::Paused) {
        playback_.Resume();
        playerView_.state = PlayerUiState::Playing;
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::Previous) {
      if (const auto* track = queueState_.Previous()) {
        BeginResolveAndPlayQueueTrack(*track);
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::Next) {
      if (const auto* track = queueState_.Next()) {
        BeginResolveAndPlayQueueTrack(*track);
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::Seek) {
      const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
      ApplyPlaybackProgress(
          playerView_,
          lyricView_,
          lyricDocument_,
          TrackValueFromPoint(bar.progress, dipX));
      playback_.Seek(DurationToSeconds(playerView_.duration) * playerView_.progress);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (playerAction == PlayerBarAction::SetVolume) {
      const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
      volume_ = TrackValueFromPoint(bar.volume, dipX);
      playback_.SetVolume(volume_);
      SaveSettingsSnapshot();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }

    const auto sidebarAction = HitTestSidebar(dipX, dipY, layout_.sidebar.bottom);
    if (sidebarAction == SidebarAction::Home) {
      NavigateTo(PageId::Home);
    } else if (sidebarAction == SidebarAction::Discover) {
      NavigateTo(PageId::Discover);
    } else if (sidebarAction == SidebarAction::Radio) {
      NavigateTo(PageId::Radio);
    } else if (sidebarAction == SidebarAction::Video) {
      NavigateTo(PageId::Video);
    } else if (sidebarAction == SidebarAction::Songs) {
      NavigateTo(PageId::Songs);
    } else if (sidebarAction == SidebarAction::Albums) {
      NavigateTo(PageId::Albums);
    } else if (sidebarAction == SidebarAction::Artists) {
      NavigateTo(PageId::Artists);
    } else if (sidebarAction == SidebarAction::NowPlaying) {
      NavigateTo(PageId::NowPlaying);
    } else if (sidebarAction == SidebarAction::Favorites) {
      NavigateTo(PageId::Favorites);
    } else if (sidebarAction == SidebarAction::Downloads) {
      NavigateTo(PageId::Downloads);
    } else if (sidebarAction == SidebarAction::Settings) {
      NavigateTo(PageId::Settings);
    } else if (dipX > clientWidth_ - 150 && dipY > clientHeight_ - 88) {
      NavigateTo(PageId::NowPlaying);
    } else if (surface_ == PageId::Home) {
      const auto homeAction = HitTestHome(layout_.home, dipX, dipY);
      if (homeAction == HomeAction::PlayHero) {
        SelectHomeTrack(queueState_.CurrentIndex());
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      const int recommendationIndex = HomeRecommendationIndexFromPoint(layout_.home, dipX, dipY);
      if (recommendationIndex >= 0) {
        SelectHomeTrack(QueueIndexWithOffset(static_cast<std::size_t>(recommendationIndex)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      const int recentIndex = HomeRecentIndexFromPoint(layout_.home, dipX, dipY);
      if (recentIndex >= 0) {
        SelectHomeTrack(QueueIndexWithOffset(static_cast<std::size_t>(recentIndex)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      const int playlistIndex = HomePlaylistIndexFromPoint(layout_.home, dipX, dipY);
      if (playlistIndex >= 0) {
        SelectHomeTrack(QueueIndexWithOffset(static_cast<std::size_t>(playlistIndex + 2)));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
    } else if (surface_ == PageId::Search) {
      const auto row = SearchRowFromPoint(dipX, dipY);
      if (row != kNoRow) {
        BeginResolveAndPlay(row);
        return;
      }
    } else if (surface_ == PageId::NowPlaying) {
      const auto nowPlayingAction = HitTestNowPlaying(layout_.nowPlaying, dipX, dipY);
      if (nowPlayingAction == NowPlayingAction::OverviewTab) {
        nowPlayingTab_ = NowPlayingTab::Overview;
      } else if (nowPlayingAction == NowPlayingAction::LyricsTab) {
        nowPlayingTab_ = NowPlayingTab::Lyrics;
      } else if (layout_.nowPlaying.showQueue) {
        const auto row = QueueRowFromPoint(dipX, dipY);
        if (row != kNoRow) {
          SelectQueueTrack(row);
        }
      }
    } else if (IsFeaturePage(surface_)) {
      const auto content = layout_.content;
      const float left = content.left + 36.0f;
      const float top = content.top + 36.0f;
      const Rect quickSearch{left + 28.0f, top + 292.0f, left + 156.0f, top + 330.0f};
      if (dipX >= quickSearch.left && dipX <= quickSearch.right && dipY >= quickSearch.top && dipY <= quickSearch.bottom) {
        const auto hint = PageSearchHint(surface_);
        BeginSearch(hint, WideToUtf8(hint));
        return;
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void HandleCharacter(wchar_t value) {
    const auto result = searchInput_.HandleCharacter(value);
    if (result.action == SearchInputAction::Submit) {
      BeginSearch(result.submittedText, WideToUtf8(result.submittedText));
      return;
    }
    if (searchInput_.IsFocused()) {
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  void HandleMouseWheel(int screenX, int screenY, int wheelDelta) {
    POINT point{screenX, screenY};
    ScreenToClient(hwnd_, &point);
    const float dipX = DevicePxToDip(static_cast<float>(point.x));
    const float dipY = DevicePxToDip(static_cast<float>(point.y));

    if (surface_ == PageId::Search) {
      const auto content = layout_.content;
      const float left = content.left + 28.0f;
      const float top = content.top + 28.0f;
      const float right = content.right - 28.0f;
      const float bottom = content.bottom - 24.0f;
      const float listTop = top + 166.0f;
      if (dipX >= left && dipX <= right && dipY >= listTop && dipY <= bottom) {
        searchScrollOffset_ = ApplyWheelScroll(
            searchView_.rows.size(),
            68.0f,
            std::max(0.0f, bottom - listTop - 20.0f),
            searchScrollOffset_,
            wheelDelta);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
    }

    if (surface_ == PageId::NowPlaying && layout_.nowPlaying.showQueue) {
      const auto queue = layout_.nowPlaying.queue;
      if (dipX >= queue.left && dipX <= queue.right && dipY >= queue.top && dipY <= queue.bottom) {
        queueScrollOffset_ = ApplyWheelScroll(
            8,
            68.0f,
            std::max(0.0f, queue.bottom - queue.top - 190.0f),
            queueScrollOffset_,
            wheelDelta);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
    }
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
    constexpr float rowHeight = 68.0f;

    if (x < left || x > right || y < listTop || y > bottom) {
      return kNoRow;
    }

    const auto index = static_cast<std::size_t>((y - listTop + searchScrollOffset_) / rowHeight);
    return index < searchView_.rows.size() ? index : kNoRow;
  }

  std::size_t QueueRowFromPoint(float x, float y) const {
    const auto queue = layout_.nowPlaying.queue;
    constexpr float rowHeight = 68.0f;
    const float listTop = queue.top + 130.0f;
    const float listBottom = queue.bottom - 58.0f;
    if (x < queue.left || x > queue.right || y < listTop || y > listBottom) {
      return kNoRow;
    }
    const auto index = static_cast<std::size_t>((y - listTop + queueScrollOffset_) / rowHeight);
    return index < queueState_.Tracks().size() ? index : kNoRow;
  }

  static int DurationToSeconds(const std::wstring& value) {
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

  static std::wstring FormatDuration(int seconds) {
    seconds = std::max(0, seconds);
    const int minutes = seconds / 60;
    const int remainder = seconds % 60;
    return std::to_wstring(minutes) + L":" + (remainder < 10 ? L"0" : L"") + std::to_wstring(remainder);
  }

  std::size_t QueueIndexWithOffset(std::size_t offset) const {
    const auto& tracks = queueState_.Tracks();
    if (tracks.empty()) {
      return 0;
    }
    return (queueState_.CurrentIndex() + offset) % tracks.size();
  }

  void SelectQueueTrack(std::size_t index) {
    const auto* track = queueState_.Select(index);
    if (!track) {
      return;
    }
    BeginResolveAndPlayQueueTrack(*track);
  }

  void SelectHomeTrack(std::size_t index) {
    SelectQueueTrack(index);
  }

  bool TryResolveNextPlaybackCandidate(const std::wstring& lastError) {
    if (!backend_) {
      return false;
    }

    if (!TryAdvancePlaybackCandidate(
            playbackCandidates_,
            playbackCandidateIndex_,
            pendingPlaybackRow_,
            playerView_,
            lyricView_,
            lastError)) {
      return false;
    }

    lyricDocument_ = {};
    songUrlFuture_ = backend_->ResolveSongUrl(pendingPlaybackRow_.hash, "");
    SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    return true;
  }

  void ApplyQueueTrack(const QueueTrack* track, PlayerUiState state) {
    if (!track) {
      return;
    }
    playerView_.title = track->title;
    playerView_.artist = track->artist;
    playerView_.album = track->album;
    playerView_.duration = track->duration;
    playerView_.current = L"00:00";
    playerView_.progress = 0.0;
    playerView_.state = state;
    playerView_.coverUrl = track->coverUrl;
    playerView_.imageKey = track->coverUrl.empty() ? std::string{} : "remote-cover:" + track->coverUrl;
    lyricView_ = BuildLyricViewModel(lyricDocument_, 0);
  }

  void Paint() {
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);

    if (SUCCEEDED(CreateDeviceResources())) {
      const auto size = CurrentClientDipSize();
      clientWidth_ = size.width;
      clientHeight_ = size.height;
      layout_ = CalculateMelodyLayout(clientWidth_, clientHeight_);

      if (renderPipeline_.BeginFrame()) {
        renderTarget_->Clear(palette_.bg);

        DrawSidebar();
        DrawHeader();
        renderTarget_->PushAxisAlignedClip(ToD2DRect(layout_.content), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (surface_ == PageId::Home) {
          DrawHome();
        } else if (surface_ == PageId::NowPlaying) {
          DrawNowPlaying();
        } else if (surface_ == PageId::Search) {
          DrawSearch();
        } else if (surface_ == PageId::Settings) {
          DrawSettings();
        } else if (surface_ == PageId::Login) {
          DrawLogin();
        } else {
          DrawFeaturePage();
        }
        renderTarget_->PopAxisAlignedClip();
        // List 19：每帧都让 GlassPanel 重新模糊（CopyFromRenderTarget 抓取此刻 backbuffer，
        // 即 player bar 上方所有已绘内容；¼ 分辨率 GaussianBlur 22px 单次 < 1ms）。
        glass_.MarkBlurDirty();
        DrawPlayerBar();

        const HRESULT hr = renderPipeline_.EndFrame();
        if (RenderPipeline::IsDeviceLossHResult(hr)) {
          DiscardDeviceResources();
        }
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
    format->SetWordWrapping(style.wrapping);
    if (style.trimming != DWRITE_TRIMMING_GRANULARITY_NONE) {
      DWRITE_TRIMMING trimming{};
      trimming.granularity = style.trimming;
      format->SetTrimming(&trimming, nullptr);
    }
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

  std::filesystem::path FindAssetPath(const std::filesystem::path& relative) const {
    std::vector<std::filesystem::path> roots;
    roots.push_back(std::filesystem::current_path());

    wchar_t modulePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
      roots.push_back(std::filesystem::path(modulePath).parent_path());
    }

    for (auto root : roots) {
      for (int i = 0; i < 8 && !root.empty(); ++i) {
        const auto candidate = root / relative;
        std::error_code error;
        if (std::filesystem::exists(candidate, error)) {
          return candidate;
        }
        root = root.parent_path();
      }
    }
    return {};
  }

  bool EnsureAppIconBitmap() {
    if (appIconBitmap_) {
      return true;
    }
    const auto* payload = appIconSlot_.Payload();
    if (!payload) {
      return false;
    }

    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        WindowDpi(),
        WindowDpi());
    return SUCCEEDED(renderTarget_->CreateBitmap(
        D2D1::SizeU(payload->width, payload->height),
        payload->bgra.data(),
        payload->width * 4,
        properties,
        &appIconBitmap_));
  }

  bool DrawBitmap(ID2D1Bitmap* bitmap, D2D1_RECT_F rect, bool fill) {
    if (!bitmap) {
      return false;
    }
    const auto size = bitmap->GetSize();
    const Rect container{rect.left, rect.top, rect.right, rect.bottom};
    const auto target = fill ? CalculateAspectFillRect(container, size.width, size.height)
                             : CalculateAspectFitRect(container, size.width, size.height);
    renderTarget_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    renderTarget_->DrawBitmap(bitmap, ToD2DRect(target), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    renderTarget_->PopAxisAlignedClip();
    return true;
  }

  bool DrawAppIcon(D2D1_RECT_F rect, bool fill = true) {
    return EnsureAppIconBitmap() && DrawBitmap(appIconBitmap_, rect, fill);
  }

  struct ArtworkState {
    ImageSlot slot;
    std::future<ImageSlotPayload> future;
    ID2D1Bitmap* bitmap = nullptr;
  };

  image::ImageLoader::RemoteFetchResult FetchRemoteImage(const std::string& url) const {
    image::ImageLoader::RemoteFetchResult fetchResult;
    const auto result = imageHttpClient_.Get(url);
    fetchResult.statusCode = result.statusCode;
    fetchResult.error = result.error;
    if (result.error.empty()) {
      fetchResult.bytes.assign(result.body.begin(), result.body.end());
    }
    return fetchResult;
  }

  void EnsureArtworkRequested(const std::string& imageKey, const std::string& imageUrl) {
    if (imageKey.empty() || imageUrl.empty()) {
      return;
    }

    auto& state = artworkStates_[imageKey];
    const auto decision = state.slot.Request(imageKey);
    if (!decision.shouldStartLoad) {
      return;
    }

    state.future = std::async(std::launch::async, [this, imageKey, imageUrl] {
      async::CancellationSource cancellation;
      auto decoded = imageLoader_.LoadRemote(
          imageKey,
          imageUrl,
          [this](const std::string& remoteUrl) { return FetchRemoteImage(remoteUrl); },
          cancellation.Token());
      if (decoded.placeholder || decoded.bgra.empty()) {
        return ImageSlotPayload{};
      }
      return ImageSlotPayload{decoded.width, decoded.height, std::move(decoded.bgra)};
    });
    SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
  }

  bool EnsureArtworkBitmap(ArtworkState& state) {
    if (state.bitmap) {
      return true;
    }

    const auto* payload = state.slot.Payload();
    if (!payload || payload->bgra.empty()) {
      return false;
    }

    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        WindowDpi(),
        WindowDpi());
    return SUCCEEDED(renderTarget_->CreateBitmap(
        D2D1::SizeU(payload->width, payload->height),
        payload->bgra.data(),
        payload->width * 4,
        properties,
        &state.bitmap));
  }

  bool DrawArtworkBitmap(const std::string& imageKey, D2D1_RECT_F rect, bool fill) {
    const auto found = artworkStates_.find(imageKey);
    if (found == artworkStates_.end()) {
      return false;
    }
    return EnsureArtworkBitmap(found->second) && DrawBitmap(found->second.bitmap, rect, fill);
  }

  void DrawArtwork(D2D1_RECT_F rect,
                   D2D1_COLOR_F fallback,
                   bool fill = true,
                   const std::string& imageKey = {},
                   const std::string& imageUrl = {}) {
    FillRound(rect, 6.0f, fallback);
    if (!imageKey.empty() && !imageUrl.empty()) {
      EnsureArtworkRequested(imageKey, imageUrl);
      if (DrawArtworkBitmap(imageKey, rect, fill)) {
        return;
      }
    }
    if (!DrawAppIcon(rect, false)) {
      Circle((rect.left + rect.right) * 0.5f,
             (rect.top + rect.bottom) * 0.5f,
             std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.18f,
             D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.30f));
    }
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
    const float h = (rect.bottom - rect.top) * 0.35f;
    const float barW = (rect.right - rect.left) * 0.12f;
    const float gap = barW * 2.2f;
    FillRound(D2D1::RectF(cx - gap * 0.5f - barW, cy - h, cx - gap * 0.5f, cy + h), 3.0f, color);
    FillRound(D2D1::RectF(cx + gap * 0.5f, cy - h, cx + gap * 0.5f + barW, cy + h), 3.0f, color);
  }

  void DrawSidebar() {
    const float sidebarBottom = layout_.sidebar.bottom;
    FillRect(D2D1::RectF(0, 0, 178, sidebarBottom), D2D1::ColorF(0.945f, 0.932f, 0.900f, 0.84f));
    StrokeLine(178, 0, 178, sidebarBottom, palette_.line);
    renderTarget_->PushAxisAlignedClip(D2D1::RectF(0, 0, 178, sidebarBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (!DrawAppIcon(D2D1::RectF(28, 31, 54, 57), false)) {
      Text(L"♪", D2D1::RectF(31, 34, 54, 66), palette_.accentDark, {21, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    }
    Text(L"BottleMusic", D2D1::RectF(62, 35, 176, 64), palette_.accentDark,
         {17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
          DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});

    struct Item {
      const wchar_t* icon;
      const wchar_t* label;
      bool active;
    };
    const std::array<Item, 10> nav = {{
        {L"⌂", L"首页", surface_ == PageId::Home},
        {L"◎", L"发现", surface_ == PageId::Discover},
        {L"◌", L"电台", surface_ == PageId::Radio},
        {L"▣", L"视频", surface_ == PageId::Video},
        {L"♪", L"歌曲", surface_ == PageId::Songs},
        {L"⊙", L"专辑", surface_ == PageId::Albums},
        {L"♙", L"歌手", surface_ == PageId::Artists},
        {L"≡", L"播放列表", surface_ == PageId::NowPlaying},
        {L"♡", L"收藏夹", surface_ == PageId::Favorites},
        {L"↧", L"下载管理", surface_ == PageId::Downloads},
    }};

    float y = 98;
    for (std::size_t i = 0; i < nav.size(); ++i) {
      if (i == 4) {
        StrokeLine(30, y - 10, 150, y - 10, palette_.line);
        Text(L"你的音乐", D2D1::RectF(31, y + 2, 150, y + 24), palette_.faint, {12});
        y += 42;
      }
      if (nav[i].active) {
        // 弱化背景（HTML 6% 在 D2D 平铺中几乎看不见，提升到 12% 增强对比）
        FillRect(D2D1::RectF(14, y - 8, 164, y + 30), D2D1::ColorF(0.133f, 0.106f, 0.071f, 0.12f));
        // 左侧红色竖条（HTML: nav a.active::before — 3px wide accent strip，比 HTML 2px 略粗以保证 100% DPI 下可见）
        FillRect(D2D1::RectF(8.0f, y - 4, 11.0f, y + 26), theme::color::Accent());
      }
      Text(nav[i].icon, D2D1::RectF(34, y, 52, y + 22),
           nav[i].active ? theme::color::Ink() : palette_.muted, {15});
      Text(nav[i].label, D2D1::RectF(63, y, 150, y + 24),
           nav[i].active ? theme::color::Ink() : palette_.text,
           {14, nav[i].active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL});
      y += 44;
    }

    const float settingsTop = sidebarBottom >= 620.0f ? sidebarBottom - 62.0f : sidebarBottom;
    if (settingsTop >= 690.0f) {
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
      for (std::size_t i = 0; i < playlists.size() && y + 24.0f < settingsTop - 10.0f; ++i) {
        FillRound(D2D1::RectF(31, y - 5, 55, y + 19), 4, colors[i]);
        Text(playlists[i], D2D1::RectF(64, y - 3, 156, y + 22), palette_.text, {13});
        y += 39;
      }
    }

    if (sidebarBottom >= 620.0f) {
      const bool active = surface_ == PageId::Settings;
      const float settingsY = sidebarBottom - 54.0f;
      if (active) {
        FillRect(D2D1::RectF(14, sidebarBottom - 60.0f, 164, sidebarBottom - 22.0f),
                 D2D1::ColorF(0.133f, 0.106f, 0.071f, 0.12f));
        FillRect(D2D1::RectF(8.0f, settingsY - 4, 11.0f, settingsY + 26), theme::color::Accent());
      }
      Text(L"⚙  设置", D2D1::RectF(34, settingsY, 150, sidebarBottom - 22.0f),
           active ? theme::color::Ink() : palette_.muted,
           {14, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL});
    }
    renderTarget_->PopAxisAlignedClip();
  }

  void DrawHeader() {
    const bool compact = clientWidth_ < 1120.0f;
    const auto controls = CalculateHeaderControlsLayout(clientWidth_, layout_.sidebar.right);
    Text(L"‹", ToD2DRect(controls.back), navigation_.CanGoBack() ? palette_.text : palette_.faint, {36});
    Text(L"›", ToD2DRect(controls.forward), navigation_.CanGoForward() ? palette_.text : palette_.faint, {36});

    const auto searchRect = ToD2DRect(controls.search);
    FillRound(searchRect, 8, D2D1::ColorF(0.95f, 0.94f, 0.91f, 0.78f));
    StrokeRound(searchRect, 8, searchInput_.IsFocused() ? palette_.accent : palette_.line);
    const auto& searchText = searchInput_.Text();
    Text(searchText.empty() ? L"搜索音乐、歌手、专辑或歌词" : searchText,
         D2D1::RectF(searchRect.left + 26, 38, searchRect.right - 58, 62),
         searchText.empty() ? palette_.faint : palette_.text, {13});
    if (searchInput_.IsFocused()) {
      const float caretLeft = std::min(searchRect.right - 60.0f, searchRect.left + 30.0f + searchText.size() * 13.0f);
      StrokeLine(caretLeft, searchRect.top + 10.0f, caretLeft, searchRect.bottom - 10.0f, palette_.accent, 1.0f);
    }
    Text(L"⌕", D2D1::RectF(searchRect.right - 36, 36, searchRect.right - 10, 62), palette_.text, {18});
    if (!compact) {
      Text(deviceStatus_, D2D1::RectF(clientWidth_ - 292, 38, clientWidth_ - 82, 62), palette_.muted,
           {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
    }
    Circle(clientWidth_ - 49, 62, 18, D2D1::ColorF(0.25f, 0.20f, 0.16f));
  }

  void DrawHome() {
    const auto& home = layout_.home;
    const auto* heroTrack = queueState_.Current();
    const float titleSize = home.compact ? 24.0f : 27.0f;

    // Newsprint masthead：报纸风格大标题横幅（双线 + 居中斜体）。
    static constexpr float kMastheadH = 32.0f;
    painter_.DrawMasthead(
        D2D1::RectF(home.greeting.left, home.greeting.top,
                    home.greeting.right, home.greeting.top + kMastheadH),
        L"BOTTLE TIMES — Vol. I");

    // 问候文本下移，为 masthead 腾出空间。
    // Newsprint 设计无 Melody "个性化推荐" 按钮，整行铺满给标题用。
    const float greetTop = home.greeting.top + kMastheadH + 8.0f;
    Text(L"早上好，开启美好的一天",
         D2D1::RectF(home.greeting.left, greetTop, home.greeting.right,
                     greetTop + 38.0f),
         palette_.text, {titleSize, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"用音乐点亮你的每一刻",
         D2D1::RectF(home.greeting.left, greetTop + 42.0f, home.greeting.right,
                     greetTop + 70.0f),
         palette_.muted, {14});

    if (IsUsable(home.hero, 260.0f, 120.0f)) {
      DrawHero(ToD2DRect(home.hero), heroTrack);
    }

    if (home.showRecommendationRow && IsUsable(home.recommendationRow, 260.0f, 156.0f)) {
      painter_.SectionHead(home.recommendationRow.left, home.recommendationRow.top,
                           home.recommendationRow.right, L"为你推荐");
      Text(L"查看全部", D2D1::RectF(home.recommendationRow.right - 72.0f, home.recommendationRow.top + 2.0f,
                                  home.recommendationRow.right, home.recommendationRow.top + 28.0f),
           palette_.accent, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      const float cardsTop = home.recommendationRow.top + 38.0f;
      const float cardHeight = std::max(108.0f, home.recommendationRow.bottom - cardsTop - 8.0f);
      DrawPlaylistCards(home.recommendationRow.left,
                        cardsTop,
                        home.recommendationRow.right,
                        home.recommendationCardCount,
                        cardHeight,
                        0);
    }

    if (home.showRecentList && IsUsable(home.recentList, 300.0f, 160.0f)) {
      painter_.SectionHead(home.recentList.left + 18.0f, home.recentList.top - 44.0f,
                           home.recentList.right, L"最近播放");
      Text(L"查看全部", D2D1::RectF(home.recentList.right - 80.0f, home.recentList.top - 42.0f,
                                  home.recentList.right, home.recentList.top - 16.0f),
           palette_.accent, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      DrawRecentList(ToD2DRect(home.recentList));
    }

    if (home.showPlaylistPanel && IsUsable(home.playlistPanel, 320.0f, 132.0f)) {
      FillRound(ToD2DRect(home.playlistPanel), 8, palette_.panel);
      StrokeRound(ToD2DRect(home.playlistPanel), 8, palette_.line);
      painter_.SectionHead(home.playlistPanel.left + 14.0f, home.playlistPanel.top + 18.0f,
                           home.playlistPanel.right - 14.0f, L"推荐歌单");
      const float cardHeight = std::max(108.0f, home.playlistPanel.bottom - home.playlistPanel.top - 68.0f);
      DrawPlaylistCards(home.playlistPanel.left + 14.0f,
                        home.playlistPanel.top + 56.0f,
                        home.playlistPanel.right - 18.0f,
                        home.playlistCardCount,
                        cardHeight,
                        2);
    }

    if (home.showArtistPanel && IsUsable(home.artistPanel, 300.0f, 200.0f)) {
      DrawArtistPanel(ToD2DRect(home.artistPanel));
    }
  }

  void DrawHero(D2D1_RECT_F rect, const QueueTrack* track) {
    if (rect.right - rect.left < 240.0f || rect.bottom - rect.top < 120.0f) {
      return;
    }
    // Newsprint .feature .hero：PaperAlt 背景 + 1px solid rule 外框 + inset 6px 内边框（近似 dashed）
    FillRect(rect, theme::color::PaperAlt());
    StrokeRound(rect, 0.0f, theme::color::Rule(), 1.0f);
    const D2D1_RECT_F insetRect = D2D1::RectF(
        rect.left + 6.0f, rect.top + 6.0f, rect.right - 6.0f, rect.bottom - 6.0f);
    if (dashStrokeStyle_) {
      renderTarget_->DrawRectangle(insetRect, EnsureSolidBrush(theme::color::RuleSoft()), 1.0f,
                                   dashStrokeStyle_);
    } else {
      StrokeRound(insetRect, 0.0f, theme::color::RuleSoft(), 1.0f);
    }

    // 封面（右侧方形 1:1，与酷狗封面图原生比例一致）。
    // 注意：直接走 DrawBitmap + AspectFill，不再借道 DrawArtwork（它会画 6px 圆角占位
    // 并在没有 URL 时退回 AspectFit 的 app-icon，导致看起来"图片没填满"）。
    const float heroH = rect.bottom - rect.top;
    const float artSide = std::min(heroH - 32.0f, 180.0f);  // 方形，留 16px 上下 padding
    const float artLeft = rect.right - 28.0f - artSide;
    const float artTop = rect.top + (heroH - artSide) * 0.5f;  // 垂直居中
    const auto artRect = D2D1::RectF(artLeft, artTop, artLeft + artSide, artTop + artSide);

    // 第 1 步：方形 Ink 占位（不带圆角），确保即使没有图片也是"满"的深色块
    FillRect(artRect, theme::color::Ink());

    // 第 2 步：尝试画真实封面 —— DrawBitmap 内部用 AspectFill + 轴对齐 clip，必然铺满
    bool coverPainted = false;
    if (track && !track->coverUrl.empty()) {
      const std::string imageKey = "remote-cover:" + track->coverUrl;
      EnsureArtworkRequested(imageKey, track->coverUrl);
      coverPainted = DrawArtworkBitmap(imageKey, artRect, true);  // fill = AspectFill
    }

    // 第 3 步：若无封面，画装饰性占位（Accent 边框 + 居中音符），仍然铺满方形
    if (!coverPainted) {
      const D2D1_RECT_F inner = D2D1::RectF(
          artRect.left + 8.0f, artRect.top + 8.0f,
          artRect.right - 8.0f, artRect.bottom - 8.0f);
      StrokeRound(inner, 0.0f, theme::color::Accent(), 1.5f);
      const float cx = (artRect.left + artRect.right) * 0.5f;
      const float cy = (artRect.top + artRect.bottom) * 0.5f;
      Text(L"♪",
           D2D1::RectF(cx - 40.0f, cy - 40.0f, cx + 40.0f, cy + 40.0f),
           theme::color::Paper(),
           {56, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER});
    }

    // Kicker（uppercase italic small label）
    painter_.Kicker(rect.left + 28, rect.top + 28, L"TODAY'S FEATURE · 今日推荐");

    // h2 标题：30px 半粗衬线（HTML: .feature .hero h2）
    Text(track ? track->title : playerView_.title,
         D2D1::RectF(rect.left + 28, rect.top + 52, artLeft - 24.0f, rect.top + 96),
         theme::color::Ink(), {30, DWRITE_FONT_WEIGHT_SEMI_BOLD});

    // 副标题：13px ink-soft；底边 = rect.top + 124
    const float subtitleBottom = rect.top + 124.0f;
    Text(track ? (track->artist + L" · " + track->album) : PlaybackSubtitle(playerView_),
         D2D1::RectF(rect.left + 28, rect.top + 100, artLeft - 24.0f, subtitleBottom),
         theme::color::InkSoft(), {13});

    // 红色播放按钮：至少在副标题下方 8px，不超过英雄区底部 54px
    const float buttonTop = std::max(subtitleBottom + 8.0f,
                                     std::min(rect.top + 140.0f, rect.bottom - 54.0f));
    FillRound(D2D1::RectF(rect.left + 28, buttonTop, rect.left + 128, buttonTop + 40.0f), 20,
              theme::color::Accent());
    DrawPlayTriangle(D2D1::RectF(rect.left + 42.0f, buttonTop + 10.0f, rect.left + 60.0f, buttonTop + 30.0f),
                     palette_.white);
    Text(L"播放", D2D1::RectF(rect.left + 62, buttonTop + 10.0f, rect.left + 122, buttonTop + 36.0f),
         palette_.white, {14, DWRITE_FONT_WEIGHT_SEMI_BOLD});
  }

  void DrawPlaylistCards(float left, float top, float right, int count, float cardHeight = 200.0f, std::size_t startIndex = 0) {
    const auto strip = CalculateCardStripLayout(right - left, count, cardHeight);
    if (strip.count <= 0) {
      return;
    }
    const auto& tracks = queueState_.Tracks();
    if (tracks.empty()) {
      return;
    }
    for (int i = 0; i < strip.count; ++i) {
      const auto& track = tracks[(startIndex + static_cast<std::size_t>(i)) % tracks.size()];
      const float x = left + i * (strip.itemWidth + strip.gap);
      FillRound(D2D1::RectF(x, top, x + strip.itemWidth, top + strip.itemHeight), 8, palette_.panel);
      StrokeRound(D2D1::RectF(x, top, x + strip.itemWidth, top + strip.itemHeight), 8, palette_.line);
      D2D1_COLOR_F color = D2D1::ColorF(0.48f + i * 0.04f, 0.67f - i * 0.03f, 0.72f - i * 0.02f);
      DrawArtwork(
          D2D1::RectF(x + 1, top + 1, x + strip.itemWidth - 1, top + strip.imageHeight),
          color,
          false,
          track.coverUrl.empty() ? std::string{} : "remote-cover:" + track.coverUrl,
          track.coverUrl);
      Circle(x + strip.itemWidth - 26, top + strip.imageHeight - 22.0f, 15, D2D1::ColorF(0.03f, 0.03f, 0.025f, 0.72f));
      DrawPlayTriangle(D2D1::RectF(x + strip.itemWidth - 35, top + strip.imageHeight - 34.0f,
                                   x + strip.itemWidth - 17, top + strip.imageHeight - 12.0f),
                       palette_.white);
      Text(track.title, D2D1::RectF(x + 12, top + strip.imageHeight + 10.0f,
                                    x + strip.itemWidth - 12, top + strip.imageHeight + 36.0f),
           palette_.text, {13, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(track.artist,
           D2D1::RectF(x + 12, top + strip.imageHeight + 36.0f, x + strip.itemWidth - 12, top + strip.itemHeight - 2.0f),
           palette_.faint, {12});
    }
  }

  void DrawRecentList(D2D1_RECT_F rect) {
    FillRound(rect, 8, palette_.panel);
    StrokeRound(rect, 8, palette_.line);
    const auto& tracks = queueState_.Tracks();
    if (tracks.empty()) {
      return;
    }
    const std::size_t visibleCount = std::min<std::size_t>(5, tracks.size());
    float y = rect.top + 18;
    for (std::size_t i = 0; i < visibleCount; ++i) {
      const auto& track = tracks[QueueIndexWithOffset(i)];
      DrawArtwork(D2D1::RectF(rect.left + 22, y - 2, rect.left + 60, y + 36),
                  D2D1::ColorF(0.58f + i * 0.05f, 0.52f, 0.36f),
                  false,
                  track.coverUrl.empty() ? std::string{} : "remote-cover:" + track.coverUrl,
                  track.coverUrl);
      Text(track.title, D2D1::RectF(rect.left + 72, y + 6, rect.left + 250, y + 30), palette_.text, {13});
      Text(track.artist, D2D1::RectF(rect.left + 238, y + 6, rect.left + 420, y + 30), palette_.muted, {12});
      Text(track.duration, D2D1::RectF(rect.right - 96, y + 6, rect.right - 48, y + 30), palette_.muted, {12});
      Text(L"•••", D2D1::RectF(rect.right - 36, y + 4, rect.right - 8, y + 30), palette_.text, {13});
      if (i + 1 < visibleCount) StrokeLine(rect.left + 70, y + 50, rect.right - 20, y + 50, palette_.line);
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
    Text(L"正在播放", D2D1::RectF(contentLeft, top, contentLeft + 120, top + 34),
         nowPlayingTab_ == NowPlayingTab::Overview ? palette_.text : palette_.muted,
         {17, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(L"歌词", D2D1::RectF(contentLeft + 120, top, contentLeft + 190, top + 34),
         nowPlayingTab_ == NowPlayingTab::Lyrics ? palette_.text : palette_.muted, {16});
    if (nowPlayingTab_ == NowPlayingTab::Overview) {
      StrokeLine(contentLeft, top + 36, contentLeft + 68, top + 36, palette_.accent, 2.0f);
    } else {
      StrokeLine(contentLeft + 120.0f, top + 36, contentLeft + 158.0f, top + 36, palette_.accent, 2.0f);
    }

    if (nowPlayingTab_ == NowPlayingTab::Lyrics) {
      const float right = layout_.nowPlaying.showQueue ? layout_.nowPlaying.queue.left - 28.0f : layout_.content.right - 28.0f;
      DrawLyrics(D2D1::RectF(layout_.content.left + 36.0f, top + 62.0f, right, layout_.content.bottom - 10.0f));
      if (layout_.nowPlaying.showQueue) {
        DrawQueue(ToD2DRect(layout_.nowPlaying.queue));
      }
    } else {
      DrawAlbumArea(ToD2DRect(layout_.nowPlaying.albumArea));
      DrawLyrics(ToD2DRect(layout_.nowPlaying.lyrics));
      if (layout_.nowPlaying.showQueue) {
        DrawQueue(ToD2DRect(layout_.nowPlaying.queue));
      }
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

    Text(L"歌曲", D2D1::RectF(left + 78, top + 116, left + 300, top + 144), palette_.muted, {13});
    Text(L"歌手", D2D1::RectF(left + 410, top + 116, left + 590, top + 144), palette_.muted, {13});
    Text(L"专辑", D2D1::RectF(left + 680, top + 116, left + 880, top + 144), palette_.muted, {13});
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

    constexpr float rowHeight = 68.0f;
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
        FillRound(D2D1::RectF(left, y - 6, right - 22, y + 56), 7, D2D1::ColorF(0.90f, 0.92f, 0.93f, 0.55f));
      }
      DrawArtwork(D2D1::RectF(left + 18, y - 2, left + 58, y + 38),
                  D2D1::ColorF(0.40f, 0.58f, 0.70f, 0.35f),
                  true,
                  row.imageKey,
                  row.coverUrl);
      Text(row.title, D2D1::RectF(left + 76, y + 2, left + 360, y + 28), palette_.text,
           {15, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(row.artist, D2D1::RectF(left + 410, y + 2, left + 640, y + 28), palette_.muted, {13});
      Text(row.album, D2D1::RectF(left + 680, y + 2, right - 140, y + 28), palette_.muted, {13});
      Text(row.duration, D2D1::RectF(right - 100, y + 2, right - 42, y + 28), palette_.muted,
           {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
      StrokeLine(left + 18, y + 56, right - 24, y + 56, palette_.line);
    }
  }

  void DrawSettings() {
    const auto content = layout_.content;
    const float left = content.left + 28.0f;
    const float top = content.top + 28.0f;
    const float right = content.right - 28.0f;

    Text(L"设置", D2D1::RectF(left, top, right, top + 42.0f), palette_.text,
         {28, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"先接入原生客户端核心开关，后续会和 EchoStorage 的配置持久化合并。",
         D2D1::RectF(left, top + 48.0f, right, top + 76.0f), palette_.muted, {14});

    const auto panel = D2D1::RectF(left, top + 104.0f, std::min(right, left + 720.0f), top + 356.0f);
    FillRound(panel, 8, palette_.panel);
    StrokeRound(panel, 8, palette_.line);
    Text(L"播放", D2D1::RectF(panel.left + 24.0f, panel.top + 22.0f, panel.right, panel.top + 52.0f),
         palette_.text, {18, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(L"启动后保持上次播放页",
         D2D1::RectF(panel.left + 24.0f, panel.top + 76.0f, panel.right - 96.0f, panel.top + 104.0f),
         palette_.text, {14});
    DrawButton(D2D1::RectF(panel.right - 86.0f, panel.top + 70.0f, panel.right - 24.0f, panel.top + 102.0f),
               L"开启", false);

    Text(L"音量", D2D1::RectF(panel.left + 24.0f, panel.top + 132.0f, panel.left + 120.0f, panel.top + 160.0f),
         palette_.text, {14});
    DrawProgress(panel.left + 120.0f, panel.top + 146.0f, panel.right - 80.0f, volume_);
    Text(std::to_wstring(static_cast<int>(std::round(volume_ * 100.0f))) + L"%",
         D2D1::RectF(panel.right - 62.0f, panel.top + 132.0f, panel.right - 24.0f, panel.top + 160.0f),
         palette_.muted, {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});

    Text(L"性能", D2D1::RectF(panel.left + 24.0f, panel.top + 188.0f, panel.right, panel.top + 218.0f),
         palette_.text, {18, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(L"目标：空闲低内存，播放中低于 180MB；图片缓存统一归 EchoImage 管理。",
         D2D1::RectF(panel.left + 24.0f, panel.top + 232.0f, panel.right - 24.0f, panel.top + 264.0f),
         palette_.muted, {14});
  }

  std::wstring PageTitle(PageId page) const {
    switch (page) {
      case PageId::Discover:
        return L"发现音乐";
      case PageId::Radio:
        return L"电台";
      case PageId::Video:
        return L"视频";
      case PageId::Songs:
        return L"歌曲库";
      case PageId::Albums:
        return L"专辑";
      case PageId::Artists:
        return L"歌手";
      case PageId::Favorites:
        return L"收藏夹";
      case PageId::Downloads:
        return L"下载管理";
      default:
        return L"功能页";
    }
  }

  bool IsFeaturePage(PageId page) const {
    return page == PageId::Discover ||
           page == PageId::Radio ||
           page == PageId::Video ||
           page == PageId::Songs ||
           page == PageId::Albums ||
           page == PageId::Artists ||
           page == PageId::Favorites ||
           page == PageId::Downloads;
  }

  std::wstring PageSearchHint(PageId page) const {
    switch (page) {
      case PageId::Discover:
        return L"推荐";
      case PageId::Radio:
        return L"电台";
      case PageId::Video:
        return L"MV";
      case PageId::Songs:
        return L"歌曲";
      case PageId::Albums:
        return L"专辑";
      case PageId::Artists:
        return L"歌手";
      case PageId::Favorites:
        return L"收藏";
      case PageId::Downloads:
        return L"下载";
      default:
        return L"音乐";
    }
  }

  void DrawFeaturePage() {
    // Newsprint 统一空态：kicker + page-head（衬线大标题 + 双线装饰）+ 说明 + 搜索 CTA
    const auto content = layout_.content;
    const float left = content.left + 36.0f;
    const float top = content.top + 36.0f;
    const float right = content.right - 36.0f;

    // Kicker：报纸式 uppercase italic 小标
    painter_.Kicker(left, top, L"COMING SOON · 即将上线");

    // Page head：22px 粗衬线标题（painter 复用）
    painter_.PageHead(left, top + 18.0f, PageTitle(surface_));

    // 装饰双线（page-head ::after）
    painter_.DoubleRule(top + 64.0f, left, std::min(right, left + 520.0f));

    // 说明：墨软色，14px 普通字体
    Text(L"该入口已接通导航。真实数据页后续补齐；",
         D2D1::RectF(left, top + 84.0f, right, top + 112.0f),
         theme::color::InkSoft(), {14});
    Text(L"现在可以用顶部搜索栏验证播放链路。",
         D2D1::RectF(left, top + 110.0f, right, top + 138.0f),
         theme::color::InkSoft(), {14});

    // 操作提示
    Text(L"快捷词：" + PageSearchHint(surface_),
         D2D1::RectF(left, top + 156.0f, right, top + 180.0f),
         theme::color::InkMute(), {13});

    // 红色 CTA 按钮
    const D2D1_RECT_F btn = D2D1::RectF(left, top + 198.0f, left + 156.0f, top + 234.0f);
    FillRound(btn, 4, theme::color::Accent());
    Text(L"用搜索验证", D2D1::RectF(btn.left, btn.top + 6.0f, btn.right, btn.bottom - 4.0f),
         palette_.white, {14, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER});
  }

  void DrawLogin() {
    const auto content = layout_.content;
    const float left = content.left + 36.0f;
    const float top = content.top + 36.0f;
    const float right = content.right - 36.0f;

    Text(L"扫码登录", D2D1::RectF(left, top, right, top + 44.0f), palette_.text,
         {30, DWRITE_FONT_WEIGHT_BOLD});
    Text(L"请使用酷狗音乐APP扫描二维码登录",
         D2D1::RectF(left, top + 54.0f, right, top + 86.0f), palette_.muted, {15});

    const auto panel = D2D1::RectF(left, top + 126.0f, std::min(right, left + 400.0f), top + 460.0f);
    FillRound(panel, 10, palette_.panel);
    StrokeRound(panel, 10, palette_.line);

    if (loginQrKey_.empty() && !isRequestingQr_ && !loginQrFuture_.valid() && backend_) {
      isRequestingQr_ = true;
      loginQrFuture_ = backend_->BeginQrLogin();
    }

    if (loginQrKey_ == "error") {
      Text(L"二维码加载失败", D2D1::RectF(panel.left, panel.top + 160.0f, panel.right, panel.top + 190.0f),
           palette_.text, {14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    } else if (!loginQrUrl_.empty()) {
      std::string encodedUrl = loginQrUrl_;
      auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = 0;
        while((start_pos = str.find(from, start_pos)) != std::string::npos) {
          str.replace(start_pos, from.length(), to);
          start_pos += to.length();
        }
      };
      replaceAll(encodedUrl, ":", "%3A");
      replaceAll(encodedUrl, "/", "%2F");
      replaceAll(encodedUrl, "?", "%3F");
      replaceAll(encodedUrl, "=", "%3D");
      replaceAll(encodedUrl, "&", "%26");

      const std::string qrImageUrl = "https://api.qrserver.com/v1/create-qr-code/?size=250x250&data=" + encodedUrl;
      const float cx = (panel.left + panel.right) * 0.5f;
      DrawArtwork(D2D1::RectF(cx - 125.0f, panel.top + 40.0f, cx + 125.0f, panel.top + 290.0f), 
                  palette_.panel, true, qrImageUrl, qrImageUrl);
      
      Text(L"等待扫描中...", D2D1::RectF(panel.left, panel.top + 300.0f, panel.right, panel.top + 330.0f),
           palette_.text, {14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    } else {
      Text(L"正在生成二维码...", D2D1::RectF(panel.left, panel.top + 160.0f, panel.right, panel.top + 190.0f),
           palette_.text, {14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    }
    Text(L"本轮先保证搜索、播放、封面、歌词和导航不串状态。",
         D2D1::RectF(panel.left + 28.0f, panel.top + 156.0f, panel.right - 28.0f, panel.top + 190.0f),
         palette_.muted, {14});
  }

  void DrawAlbumArea(D2D1_RECT_F rect) {
    const float availableWidth = std::max(260.0f, rect.right - rect.left);
    const float availableHeight = std::max(320.0f, rect.bottom - rect.top);
    const float coverSize = std::clamp(std::min(availableWidth - 32.0f, availableHeight * 0.48f), 240.0f, 360.0f);
    const bool showVinyl = availableWidth >= 390.0f;
    const std::wstring albumBadge = playerView_.album.empty() ? L"专辑" : playerView_.album;
    const std::wstring artistBadge = playerView_.artist.empty() ? L"艺人" : playerView_.artist;
    const std::wstring currentTime = playerView_.current.empty() ? L"00:00" : playerView_.current;
    FillRound(D2D1::RectF(rect.left - 12.0f, rect.top - 10.0f, rect.right, rect.bottom), 12.0f, palette_.panel);
    StrokeRound(D2D1::RectF(rect.left - 12.0f, rect.top - 10.0f, rect.right, rect.bottom), 12.0f, palette_.line);
    DrawArtwork(D2D1::RectF(rect.left, rect.top, rect.left + coverSize, rect.top + coverSize),
                D2D1::ColorF(0.20f, 0.15f, 0.12f),
                true,
                playerView_.imageKey,
                playerView_.coverUrl);
    if (showVinyl) {
      Circle(rect.left + coverSize + 5.0f, rect.top + coverSize * 0.5f, coverSize * 0.29f,
             D2D1::ColorF(0.04f, 0.05f, 0.055f, 0.88f));
      Circle(rect.left + coverSize + 5.0f, rect.top + coverSize * 0.5f, coverSize * 0.11f,
             D2D1::ColorF(0.12f, 0.13f, 0.13f));
    }
    Text(albumBadge, D2D1::RectF(rect.left + coverSize - 142.0f, rect.top + 26, rect.left + coverSize - 18.0f, rect.top + 92),
         D2D1::ColorF(0.82f, 0.62f, 0.30f), {18, DWRITE_FONT_WEIGHT_SEMI_BOLD});
    Text(artistBadge, D2D1::RectF(rect.left + coverSize * 0.20f, rect.top + coverSize - 62.0f,
                               rect.left + coverSize - 80.0f, rect.top + coverSize - 10.0f),
         D2D1::ColorF(0.82f, 0.62f, 0.30f), {coverSize < 330.0f ? 22.0f : 27.0f, DWRITE_FONT_WEIGHT_BOLD});

    const float detailTop = rect.top + coverSize + 22.0f;
    Text(playerView_.title, D2D1::RectF(rect.left, detailTop, rect.left + availableWidth - 24.0f, detailTop + 38.0f), palette_.text,
         {coverSize < 330.0f ? 23.0f : 28.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING,
          DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    Text(PlaybackSubtitle(playerView_),
         D2D1::RectF(rect.left, detailTop + 56.0f, rect.left + availableWidth - 24.0f, detailTop + 86.0f),
         palette_.text,
         {18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
          DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    Text(L"专辑 · " + playerView_.album, D2D1::RectF(rect.left, detailTop + 96.0f, rect.left + availableWidth - 24.0f, detailTop + 122.0f), palette_.muted,
         {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
          DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    DrawButton(D2D1::RectF(rect.left, detailTop + 132.0f, rect.left + 84.0f, detailTop + 164.0f), L"HQ 音质", false);
    DrawButton(D2D1::RectF(rect.left + 98.0f, detailTop + 132.0f, rect.left + 166.0f, detailTop + 164.0f), L"已关注", false);
    Text(L"♥", D2D1::RectF(rect.left + availableWidth - 78.0f, detailTop + 2.0f, rect.left + availableWidth - 46.0f, detailTop + 38.0f),
         palette_.accent, {26});
    Text(L"•••", D2D1::RectF(rect.left + availableWidth - 36.0f, detailTop + 8.0f, rect.left + availableWidth, detailTop + 34.0f),
         palette_.muted, {18});
    const float progressTop = std::min(detailTop + 200.0f, rect.bottom - 46.0f);
    DrawProgress(rect.left, progressTop, rect.left + std::min(360.0f, availableWidth - 24.0f), playerView_.progress);
    Text(currentTime, D2D1::RectF(rect.left, progressTop + 12.0f, rect.left + 60.0f, progressTop + 36.0f), palette_.muted, {12});
    Text(playerView_.duration, D2D1::RectF(rect.left + availableWidth - 76.0f, progressTop + 12.0f, rect.left + availableWidth - 16.0f, progressTop + 36.0f), palette_.muted, {12});
  }

  void DrawLyrics(D2D1_RECT_F rect) {
    FillRound(D2D1::RectF(rect.left - 12.0f, rect.top - 10.0f, rect.right + 12.0f, rect.bottom), 12.0f,
              D2D1::ColorF(0.985f, 0.976f, 0.955f, 0.62f));
    StrokeRound(D2D1::RectF(rect.left - 12.0f, rect.top - 10.0f, rect.right + 12.0f, rect.bottom), 12.0f, palette_.line);
    Text(playerView_.title + L" - " + playerView_.artist, D2D1::RectF(rect.left, rect.top + 10, rect.right, rect.top + 42), palette_.text,
         {20, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,
          DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    Text(L"词：BottleMusic    曲：Native Preview", D2D1::RectF(rect.left, rect.top + 50, rect.right, rect.top + 78), palette_.muted,
         {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    if (lyricView_.state == LyricUiState::Empty) {
      Text(lyricView_.message, D2D1::RectF(rect.left, rect.top + 220, rect.right, rect.top + 260), palette_.muted,
           {18, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      return;
    }

    const float footerTop = rect.bottom - 60.0f;
    const float clipTop = rect.top + 88.0f;
    const std::size_t visibleCount = static_cast<std::size_t>(
        std::max(1.0f, std::floor(std::max(42.0f, footerTop - clipTop - 12.0f) / 44.0f)));
    const auto firstLine = FirstVisibleLyricLine(lyricView_.lines.size(), lyricView_.activeIndex, visibleCount);
    const auto lastLine = std::min(lyricView_.lines.size(), firstLine + visibleCount);
    float y = rect.top + 100.0f;
    renderTarget_->PushAxisAlignedClip(
        D2D1::RectF(rect.left, clipTop, rect.right, footerTop - 12.0f),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (std::size_t i = firstLine; i < lastLine; ++i) {
      const bool active = lyricView_.lines[i].active;
      const float lineHeight = active ? 48.0f : 40.0f;
      if (y + lineHeight > footerTop - 8.0f) {
        break;
      }
      Text(lyricView_.lines[i].text, D2D1::RectF(rect.left, y, rect.right, y + 38),
           active ? palette_.accent : D2D1::ColorF(0.38f, 0.38f, 0.36f),
           {active ? 24.0f : 18.0f,
            active ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_TEXT_ALIGNMENT_CENTER,
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
            DWRITE_WORD_WRAPPING_NO_WRAP,
            DWRITE_TRIMMING_GRANULARITY_CHARACTER});
      y += lineHeight;
    }
    renderTarget_->PopAxisAlignedClip();
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

    const auto& tracks = queueState_.Tracks();
    constexpr float rowHeight = 68.0f;
    const auto visibleRows = CalculateVisibleRows(
        tracks.size(),
        rowHeight,
        0.0f,
        queueScrollOffset_,
        std::max(0.0f, rect.bottom - rect.top - 190.0f),
        1);
    for (std::size_t i = visibleRows.first; i < visibleRows.lastExclusive; ++i) {
      const auto& track = tracks[i];
      const float y = rect.top + 130.0f + static_cast<float>(i) * rowHeight - queueScrollOffset_;
      const bool active = i == queueState_.CurrentIndex();
      if (active) FillRound(D2D1::RectF(rect.left + 12, y - 10, rect.right - 12, y + 58), 7, palette_.panelStrong);
      Text(active ? L"▮▮" : std::to_wstring(i + 1), D2D1::RectF(rect.left + 24, y + 8, rect.left + 50, y + 34),
           active ? palette_.accent : palette_.text, {13});
      DrawArtwork(D2D1::RectF(rect.left + 54, y - 1, rect.left + 88, y + 33),
                  D2D1::ColorF(0.24f + static_cast<float>(i) * 0.04f, 0.34f, 0.40f),
                  true,
                  track.coverUrl.empty() ? std::string{} : "remote-cover:" + track.coverUrl,
                  track.coverUrl);
      Text(track.title, D2D1::RectF(rect.left + 98, y, rect.right - 90, y + 26), palette_.text,
           {14, DWRITE_FONT_WEIGHT_SEMI_BOLD});
      Text(track.artist, D2D1::RectF(rect.left + 98, y + 26, rect.right - 90, y + 50), palette_.muted, {12});
      Text(track.duration, D2D1::RectF(rect.right - 76, y + 8, rect.right - 20, y + 36), palette_.muted, {12});
      if (i + 1 < tracks.size()) StrokeLine(rect.left + 98, y + 62, rect.right - 24, y + 62, palette_.line);
    }
    Text(std::to_wstring(tracks.size()) + L" 首歌曲 · 33 分钟",
         D2D1::RectF(rect.left + 24, rect.bottom - 42, rect.left + 180, rect.bottom - 16),
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

  std::wstring PlaybackStateLabel() const {
    switch (playerView_.state) {
      case PlayerUiState::Resolving:
        return L"解析中";
      case PlayerUiState::Ready:
        return L"就绪";
      case PlayerUiState::Playing:
        return L"播放中";
      case PlayerUiState::Paused:
        return L"已暂停";
      case PlayerUiState::Error:
        return L"错误";
      case PlayerUiState::Idle:
      default:
        return L"待播放";
    }
  }

  D2D1_COLOR_F PlaybackStateColor() const {
    if (playerView_.state == PlayerUiState::Error) {
      return D2D1::ColorF(0.72f, 0.18f, 0.16f);
    }
    if (playerView_.state == PlayerUiState::Playing) {
      return palette_.accent;
    }
    if (playerView_.state == PlayerUiState::Resolving) {
      return D2D1::ColorF(0.72f, 0.48f, 0.10f);
    }
    return palette_.muted;
  }

  void DrawPlayerBar() {
    const auto bar = CalculatePlayerBarLayout(clientWidth_, clientHeight_);
    const D2D1_RECT_F barRect = D2D1::RectF(bar.bar.left, bar.bar.top - 1.0f,
                                            bar.bar.right, bar.bar.bottom);
    // List 19：玻璃面板（毛玻璃 + 纸纹 + 高光边）。glass_ 未就绪时（CI / 设备丢失中）
    // 退回原 Paper 纯色，保证视觉不致出现透明黑色背景。
    if (glass_.ready()) {
      glass_.DrawGlassPanel(
          barRect,
          theme::color::GlassTint(),
          theme::color::GlassEdge(),
          &paperTex_,
          0.22f);
    } else {
      FillRect(barRect, palette_.panel);
    }
    StrokeLine(bar.bar.left, bar.bar.top, bar.bar.right, bar.bar.top, palette_.line);
    StrokeLine(bar.progress.left, bar.bar.top + 8.0f, bar.progress.right, bar.bar.top + 8.0f,
               D2D1::ColorF(0.64f, 0.64f, 0.60f, 0.28f), 2.0f);
    StrokeLine(bar.progress.left, bar.bar.top + 8.0f,
               bar.progress.left + (bar.progress.right - bar.progress.left) * static_cast<float>(playerView_.progress),
               bar.bar.top + 8.0f, palette_.accent, 2.0f);
    DrawArtwork(ToD2DRect(bar.albumArt),
                D2D1::ColorF(0.20f, 0.15f, 0.12f),
                true,
                playerView_.imageKey,
                playerView_.coverUrl);
    Text(playerView_.title, ToD2DRect(bar.title), palette_.text,
         {16, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
          DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    Text(PlaybackSubtitle(playerView_), ToD2DRect(bar.artist), palette_.muted,
         {13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
          DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TRIMMING_GRANULARITY_CHARACTER});
    if (bar.showFavorite) {
      Text(L"♥", ToD2DRect(bar.favorite), palette_.accent, {22});
    }

    const float badgeLeft = bar.albumArt.right + 18.0f;
    const auto badgeRect = D2D1::RectF(badgeLeft, bar.bar.top + 82.0f, badgeLeft + 72.0f, bar.bar.top + 104.0f);
    FillRound(badgeRect, 11.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.42f));
    StrokeRound(badgeRect, 11.0f, PlaybackStateColor());
    Text(PlaybackStateLabel(), D2D1::RectF(badgeRect.left, badgeRect.top + 3.0f, badgeRect.right, badgeRect.bottom),
         PlaybackStateColor(), {12, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER});
    const std::wstring sourceText = playerView_.sourceUrl.empty() ? deviceStatus_ : L"音频流已连接";
    Text(sourceText, D2D1::RectF(badgeRect.right + 8.0f, badgeRect.top + 3.0f, std::min(bar.progress.left - 16.0f, badgeRect.right + 220.0f), badgeRect.bottom),
         palette_.faint, {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING,
                          DWRITE_PARAGRAPH_ALIGNMENT_NEAR, DWRITE_WORD_WRAPPING_NO_WRAP,
                          DWRITE_TRIMMING_GRANULARITY_CHARACTER});

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
    Text(playerView_.current, ToD2DRect(bar.currentTime), palette_.muted,
         {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING});
    DrawProgress(bar.progress.left, bar.progress.top, bar.progress.right, static_cast<float>(playerView_.progress));
    Text(playerView_.duration, ToD2DRect(bar.duration), palette_.muted, {12});

    if (bar.showVolume) {
      Text(L"音量", D2D1::RectF(bar.volumeIcon.left - 22.0f, bar.volumeIcon.top + 2.0f, bar.volumeIcon.right + 38.0f,
                                bar.volumeIcon.bottom),
           palette_.muted, {12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
      DrawProgress(bar.volume.left, bar.volume.top, bar.volume.right, volume_);
      Text(std::to_wstring(static_cast<int>(std::round(volume_ * 100.0f))) + L"%",
           D2D1::RectF(bar.volume.right + 8.0f, bar.volume.top - 11.0f, bar.volume.right + 48.0f, bar.volume.top + 12.0f),
           palette_.faint, {12});
    }
    Text(L"≡", ToD2DRect(bar.queue), palette_.muted, {22, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER});
    DrawButton(ToD2DRect(bar.lyric), L"词", false);
  }

  HWND hwnd_ = nullptr;
  // 渲染管线（拥有 Factory1 / D3D11 / D2D Device / DeviceContext / SwapChain1 / 后台缓冲）。
  // d2dFactory_ 与 renderTarget_ 是其内部资源的非拥有指针缓存，便于现有 ~160 处
  // renderTarget_->X() / d2dFactory_->Y() 调用无须批量改写。
  RenderPipeline renderPipeline_;
  ID2D1Factory1* d2dFactory_ = nullptr;
  ID2D1DeviceContext* renderTarget_ = nullptr;
  ID2D1Bitmap* appIconBitmap_ = nullptr;
  IDWriteFactory* writeFactory_ = nullptr;
  // 虚线笔刷样式（D2D1Factory 级别对象，工厂存活期间复用，不随设备丢失重建）。
  // 用于 Newsprint hero 内边框（.feature .hero::before 6px inset dashed）。
  ID2D1StrokeStyle* dashStrokeStyle_ = nullptr;
  // 短期 brush（按颜色一次性创建，绘制完即释放）。用于偶发的非 palette 颜色绘制。
  ID2D1SolidColorBrush* EnsureSolidBrush(const D2D1_COLOR_F& color) {
    if (transientBrush_) { transientBrush_->Release(); transientBrush_ = nullptr; }
    if (renderTarget_) renderTarget_->CreateSolidColorBrush(color, &transientBrush_);
    return transientBrush_;
  }
  ID2D1SolidColorBrush* transientBrush_ = nullptr;
  // Newsprint 排版绘制助手（TextFormat 缓存在 InitializeFonts 后跨设备复用；
  // 笔刷在 AttachContext / DetachContext 随设备生命周期管理）。
  Painter painter_;
  // List 19：玻璃面板 + 纸纹（设备相关，CreateDeviceResources/DiscardDeviceResources 管理）。
  GlassPanel    glass_;
  PaperTexture  paperTex_;
  ImageSlot appIconSlot_;
  std::future<ImageSlotPayload> appIconFuture_;
  Palette palette_ = MakeNewsprintPalette();
  void NavigateTo(PageId page) {
    navigation_.NavigateTo(page);
    surface_ = navigation_.Current();
  }

  void SaveSettingsSnapshot() {
    if (!backend_) {
      return;
    }
    core::AppSettings settings;
    settings.volume = volume_;
    settings.startupPage = surface_ == PageId::NowPlaying ? "now_playing" : "home";
    settings.imageMemoryCacheMb = 32;
    try {
      settingsSaveFuture_ = backend_->SaveSettings(settings);
      SetTimer(hwnd_, kBackendPollTimer, 100, nullptr);
    } catch (...) {
      deviceStatus_ = L"设置保存失败";
    }
  }

  NavigationState navigation_;
  PageId surface_ = PageId::Home;
  NowPlayingTab nowPlayingTab_ = NowPlayingTab::Overview;
  std::unique_ptr<core::IBackendFacade> backend_;
  std::future<std::unique_ptr<core::IBackendFacade>> backendFuture_;
  std::future<core::DeviceInfo> deviceFuture_;
  std::future<core::AppSettings> settingsFuture_;
  std::future<void> settingsSaveFuture_;
  core::DeviceInfo device_;
  std::wstring deviceStatus_ = L"设备未初始化";
  float clientWidth_ = 1600.0f;
  float clientHeight_ = 1060.0f;
  MelodyLayout layout_ = CalculateMelodyLayout(1600.0f, 1060.0f);
  image::MemoryImageCache imageMemoryCache_{32 * 1024 * 1024};
  image::DiskImageCache imageDiskCache_{storage::GetAppDataDirectory() / L"image-cache", 128 * 1024 * 1024};
  image::ImageLoader imageLoader_{imageMemoryCache_, imageDiskCache_};
  core::HttpClient imageHttpClient_;
  std::unordered_map<std::string, ArtworkState> artworkStates_;
  float queueScrollOffset_ = 0.0f;
  SearchViewModel searchView_;
  std::future<nlohmann::json> searchFuture_;
  std::string searchKeyword_;
  std::future<nlohmann::json> queuePlaybackSearchFuture_;
  std::string queuePlaybackSearchKeyword_;
  SearchResultRow pendingQueuePlaybackRow_;
  SearchInputState searchInput_;
  float searchScrollOffset_ = 0.0f;
  playback::PlaybackController playback_;
  bool playbackInitialized_ = false;
  float volume_ = 0.48f;
  PlaybackQueueState queueState_ = PlaybackQueueState({
      {L"晴天", L"周杰伦", L"叶惠美", L"04:29", "http://imge.kugou.com/stdmusic/480/20230920/20230920142503632013.jpg"},
      {L"七里香", L"周杰伦", L"七里香", L"04:57", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"一路向北", L"周杰伦", L"Initial J", L"04:55", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"稻香", L"周杰伦", L"魔杰座", L"03:43", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"夜曲", L"周杰伦", L"十一月的萧邦", L"03:48", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"不能说的秘密", L"周杰伦", L"不能说的秘密", L"04:56", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"简单爱", L"周杰伦", L"范特西", L"04:30", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
      {L"轨迹", L"周杰伦", L"寻找周杰伦", L"04:41", "http://imge.kugou.com/stdmusic/480/20150720/20150720110818420908.jpg"},
  });
  PlaybackViewModel playerView_;
  SearchResultRow pendingPlaybackRow_;
  std::vector<SearchResultRow> playbackCandidates_;
  std::size_t playbackCandidateIndex_ = 0;
  std::future<nlohmann::json> songUrlFuture_;
  std::future<nlohmann::json> lyricSearchFuture_;
  std::future<nlohmann::json> lyricDetailFuture_;
  core::LyricDocument lyricDocument_;
  LyricViewModel lyricView_;
  std::int64_t playbackPositionMs_ = 102000;
  
  bool isRequestingQr_ = false;
  std::string loginQrKey_;
  std::string loginQrUrl_;
  std::future<nlohmann::json> loginQrFuture_;
  std::future<nlohmann::json> loginPollFuture_;
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

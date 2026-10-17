#include "MediaIntegration.h"
#include "music/MusicController.h"
#include <QCoreApplication>
#include <QQuickWindow>
#include <QPointer>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <QDir>
#include <SystemMediaTransportControlsInterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

struct MediaIntegration::Impl {
    bool available = false;
#ifdef Q_OS_WIN
    winrt::Windows::Media::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttons{};
    HWND window = nullptr;
    NOTIFYICONDATAW tray{};
    HICON icon = nullptr;
    QPointer<QQuickWindow> quickWindow;
#endif
};
MediaIntegration::MediaIntegration(MusicController& music, QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()), music_(music) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(&music_, &MusicController::playbackChanged, this, &MediaIntegration::update);
    connect(&music_, &MusicController::queueChanged, this, &MediaIntegration::update);
}
MediaIntegration::~MediaIntegration() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
#ifdef Q_OS_WIN
    try { if (impl_->controls) { impl_->controls.ButtonPressed(impl_->buttons); impl_->controls.IsEnabled(false); } } catch (...) { }
    if (impl_->tray.hWnd) Shell_NotifyIconW(NIM_DELETE, &impl_->tray);
    if (impl_->icon) DestroyIcon(impl_->icon);
#endif
}
bool MediaIntegration::available() const { return impl_->available; }
void MediaIntegration::attach(QQuickWindow* window) {
#ifdef Q_OS_WIN
    if (!window || impl_->available) return;
    impl_->quickWindow = window;
    impl_->window = reinterpret_cast<HWND>(window->winId());
    const auto iconPath = QDir(QCoreApplication::applicationDirPath()).filePath("BottleMusic.ico");
    impl_->icon = static_cast<HICON>(LoadImageW(nullptr, reinterpret_cast<const wchar_t*>(iconPath.utf16()), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
    if (impl_->icon) {
        impl_->tray.cbSize = sizeof(NOTIFYICONDATAW); impl_->tray.hWnd = impl_->window; impl_->tray.uID = 1;
        impl_->tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; impl_->tray.hIcon = impl_->icon; impl_->tray.uCallbackMessage = WM_APP + 23;
        wcscpy_s(impl_->tray.szTip, L"BottleMusic"); Shell_NotifyIconW(NIM_ADD, &impl_->tray);
    }
    try {
        using namespace winrt::Windows::Media;
        impl_->window = reinterpret_cast<HWND>(window->winId());
        const auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        winrt::check_hresult(interop->GetForWindow(impl_->window, winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi(impl_->controls)));
        const QPointer<MediaIntegration> owner(this);
        impl_->buttons = impl_->controls.ButtonPressed([owner](auto const&, SystemMediaTransportControlsButtonPressedEventArgs const& args) {
            int command = 0;
            switch (args.Button()) { case SystemMediaTransportControlsButton::Play: command = 1; break; case SystemMediaTransportControlsButton::Pause: command = 2; break;
                case SystemMediaTransportControlsButton::Next: command = 3; break; case SystemMediaTransportControlsButton::Previous: command = 4; break; default: return; }
            if (owner) QMetaObject::invokeMethod(owner, [owner, command] { if (owner) owner->button(command); }, Qt::QueuedConnection);
        });
        impl_->controls.IsEnabled(true); impl_->available = true; update(); emit changed();
    } catch (...) { impl_->available = false; emit changed(); }
#else
    Q_UNUSED(window);
#endif
}
void MediaIntegration::button(int command) {
    if (command == 1) music_.setPlaying(true); else if (command == 2) music_.setPlaying(false);
    else if (command == 3) music_.next(); else if (command == 4) music_.previous(); else music_.toggle();
}
void MediaIntegration::update() {
#ifdef Q_OS_WIN
    if (!impl_->available) return;
    try {
        using namespace winrt::Windows::Media;
        const auto track = music_.current(); const bool selected = music_.queueIndex() >= 0;
        impl_->controls.IsPlayEnabled(selected); impl_->controls.IsPauseEnabled(selected);
        impl_->controls.IsNextEnabled(selected); impl_->controls.IsPreviousEnabled(selected && !music_.radio());
        impl_->controls.PlaybackStatus(music_.playing() ? MediaPlaybackStatus::Playing : selected ? MediaPlaybackStatus::Paused : MediaPlaybackStatus::Stopped);
        const auto updater = impl_->controls.DisplayUpdater(); updater.Type(MediaPlaybackType::Music);
        const auto utf16 = [](const QString& text) { return winrt::hstring(reinterpret_cast<const wchar_t*>(text.utf16()), uint32_t(text.size())); };
        updater.MusicProperties().Title(utf16(track.value("title").toString())); updater.MusicProperties().Artist(utf16(track.value("artist").toString())); updater.MusicProperties().AlbumTitle(utf16(track.value("album").toString()));
        const QUrl artwork(track.value("image").toString());
        if (artwork.isValid() && (artwork.scheme() == "https" || artwork.scheme() == "http"))
            updater.Thumbnail(winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromUri(winrt::Windows::Foundation::Uri(utf16(artwork.toString()))));
        else updater.Thumbnail(nullptr);
        updater.Update();
    } catch (...) { }
#endif
}
bool MediaIntegration::nativeEventFilter(const QByteArray&, void* raw, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* message = static_cast<MSG*>(raw);
    if (message->message == WM_APP + 23 && message->hwnd == impl_->window) {
        if (message->lParam == WM_LBUTTONDBLCLK && impl_->quickWindow) { impl_->quickWindow->showNormal(); impl_->quickWindow->requestActivate(); }
        else if (message->lParam == WM_RBUTTONUP) {
            const HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, 1, L"显示窗口"); AppendMenuW(menu, MF_STRING, 2, L"播放 / 暂停");
            AppendMenuW(menu, MF_STRING, 3, L"上一首"); AppendMenuW(menu, MF_STRING, 4, L"下一首"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, 5, L"退出");
            POINT point; GetCursorPos(&point); SetForegroundWindow(impl_->window);
            const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, impl_->window, nullptr); DestroyMenu(menu);
            if (selected == 1 && impl_->quickWindow) { impl_->quickWindow->showNormal(); impl_->quickWindow->requestActivate(); }
            else if (selected == 2) button(0); else if (selected == 3) button(4); else if (selected == 4) button(3); else if (selected == 5) QCoreApplication::quit();
        }
        *result = 1; return true;
    }
    if (message->message != WM_APPCOMMAND || message->hwnd != impl_->window) return false;
    const int command = GET_APPCOMMAND_LPARAM(message->lParam);
    if (command == APPCOMMAND_MEDIA_PLAY_PAUSE) button(0);
    else if (command == APPCOMMAND_MEDIA_PLAY) button(1);
    else if (command == APPCOMMAND_MEDIA_PAUSE) button(2);
    else if (command == APPCOMMAND_MEDIA_NEXTTRACK) button(3);
    else if (command == APPCOMMAND_MEDIA_PREVIOUSTRACK) button(4);
    else return false;
    *result = 1; return true;
#else
    Q_UNUSED(raw); Q_UNUSED(result); return false;
#endif
}

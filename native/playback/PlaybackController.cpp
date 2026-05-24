#include "echo/playback/PlaybackController.h"

#include <mfplay.h>
#include <mfapi.h>
#include <objbase.h>
#include <propidl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>
#include <string>
#include <windows.h>

namespace echo::playback {
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

std::string HResultMessage(const char* prefix, HRESULT hr) {
  std::ostringstream stream;
  stream << prefix << " (0x" << std::hex << static_cast<unsigned int>(hr) << ")";
  return stream.str();
}

double PropVariant100NsToSeconds(const PROPVARIANT& value) {
  if (value.vt == VT_I8) {
    return static_cast<double>(value.hVal.QuadPart) / 10000000.0;
  }
  if (value.vt == VT_UI8) {
    return static_cast<double>(value.uhVal.QuadPart) / 10000000.0;
  }
  return 0.0;
}

}  // namespace

class MediaPlayerCallback final : public IMFPMediaPlayerCallback {
 public:
  explicit MediaPlayerCallback(PlaybackController* owner) : owner_(owner) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (!object) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IMFPMediaPlayerCallback) {
      *object = static_cast<IMFPMediaPlayerCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = InterlockedDecrement(&refCount_);
    if (count == 0) {
      delete this;
    }
    return static_cast<ULONG>(count);
  }

  void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* eventHeader) override {
    auto* owner = owner_.load();
    if (owner) {
      owner->HandleMediaEvent(eventHeader);
    }
  }

  void Detach() {
    owner_.store(nullptr);
  }

 private:
  volatile long refCount_ = 1;
  std::atomic<PlaybackController*> owner_;
};

PlaybackController::PlaybackController() = default;

PlaybackController::~PlaybackController() {
  {
    std::lock_guard lock(mutex_);
    if (callback_) {
      callback_->Detach();
    }
    ReleasePlayerLocked();
  }
  if (callback_) {
    callback_->Release();
    callback_ = nullptr;
  }
  if (mediaFoundationStarted_) {
    MFShutdown();
  }
  if (comInitialized_) {
    CoUninitialize();
  }
}

bool PlaybackController::Initialize() {
  std::lock_guard lock(mutex_);
  if (mediaFoundationStarted_) return true;

  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(comHr)) {
    comInitialized_ = true;
  } else if (comHr != RPC_E_CHANGED_MODE) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.error = HResultMessage("CoInitializeEx failed", comHr);
    return false;
  }

  const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.error = HResultMessage("MFStartup failed", hr);
    return false;
  }
  mediaFoundationStarted_ = true;
  return true;
}

bool PlaybackController::PlayUrl(const std::string& url) {
  std::lock_guard lock(mutex_);
  if (!mediaFoundationStarted_) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.error = "Media Foundation is not initialized";
    return false;
  }
  ReleasePlayerLocked();
  if (url.empty()) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.sourceUrl.clear();
    state_.error = "Playback URL is empty";
    return false;
  }
  if (!EnsurePlayerLocked()) {
    return false;
  }

  const auto wideUrl = Utf8ToWide(url);
  const HRESULT hr = player_->CreateMediaItemFromURL(wideUrl.c_str(), FALSE, 0, nullptr);
  if (FAILED(hr)) {
    ReleasePlayerLocked();
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.sourceUrl.clear();
    state_.error = HResultMessage("CreateMediaItemFromURL failed", hr);
    return false;
  }

  state_.sourceUrl = url;
  state_.currentSeconds = 0.0;
  state_.durationSeconds = 0.0;
  state_.kind = echo::core::PlaybackStateKind::Opening;
  state_.error.clear();
  return true;
}

void PlaybackController::Pause() {
  std::lock_guard lock(mutex_);
  if (player_) {
    player_->Pause();
  }
  if (state_.kind == echo::core::PlaybackStateKind::Playing ||
      state_.kind == echo::core::PlaybackStateKind::Opening) {
    state_.kind = echo::core::PlaybackStateKind::Paused;
  }
}

void PlaybackController::Resume() {
  std::lock_guard lock(mutex_);
  if (player_) {
    player_->Play();
  }
  if (state_.kind == echo::core::PlaybackStateKind::Paused) {
    state_.kind = echo::core::PlaybackStateKind::Playing;
  }
}

void PlaybackController::Stop() {
  std::lock_guard lock(mutex_);
  ReleasePlayerLocked();
  if (state_.kind != echo::core::PlaybackStateKind::Idle) {
    state_.kind = echo::core::PlaybackStateKind::Stopped;
    state_.currentSeconds = 0.0;
    state_.sourceUrl.clear();
  }
}

void PlaybackController::Seek(double seconds) {
  std::lock_guard lock(mutex_);
  state_.currentSeconds = std::max(0.0, seconds);
  if (player_) {
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = static_cast<LONGLONG>(std::llround(state_.currentSeconds * 10000000.0));
    player_->SetPosition(MFP_POSITIONTYPE_100NS, &position);
    PropVariantClear(&position);
  }
}

void PlaybackController::SetVolume(double volume) {
  std::lock_guard lock(mutex_);
  state_.volume = std::clamp(volume, 0.0, 1.0);
  if (player_) {
    player_->SetVolume(static_cast<float>(state_.volume));
  }
}

void PlaybackController::SetRate(double rate) {
  std::lock_guard lock(mutex_);
  state_.rate = std::clamp(rate, 0.5, 2.0);
  if (player_) {
    player_->SetRate(static_cast<float>(state_.rate));
  }
}

echo::core::PlaybackState PlaybackController::GetState() const {
  std::lock_guard lock(mutex_);
  auto snapshot = state_;
  if (player_ && (state_.kind == echo::core::PlaybackStateKind::Opening ||
                  state_.kind == echo::core::PlaybackStateKind::Playing ||
                  state_.kind == echo::core::PlaybackStateKind::Paused)) {
    PROPVARIANT position;
    PropVariantInit(&position);
    if (SUCCEEDED(player_->GetPosition(MFP_POSITIONTYPE_100NS, &position))) {
      const double seconds = PropVariant100NsToSeconds(position);
      if (seconds >= 0.0) {
        snapshot.currentSeconds = seconds;
      }
    }
    PropVariantClear(&position);

    PROPVARIANT duration;
    PropVariantInit(&duration);
    if (SUCCEEDED(player_->GetDuration(MFP_POSITIONTYPE_100NS, &duration))) {
      const double seconds = PropVariant100NsToSeconds(duration);
      if (seconds > 0.0) {
        snapshot.durationSeconds = seconds;
      }
    }
    PropVariantClear(&duration);
  }
  return snapshot;
}

void PlaybackController::ReleasePlayerLocked() {
  if (!player_) {
    return;
  }
  player_->Stop();
  player_->Shutdown();
  player_->Release();
  player_ = nullptr;
}

bool PlaybackController::EnsurePlayerLocked() {
  if (player_) {
    return true;
  }
  if (!callback_) {
    callback_ = new MediaPlayerCallback(this);
  }
  const HRESULT hr = MFPCreateMediaPlayer(
      nullptr,
      FALSE,
      MFP_OPTION_NONE,
      callback_,
      nullptr,
      &player_);
  if (FAILED(hr)) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.sourceUrl.clear();
    state_.error = HResultMessage("MFPCreateMediaPlayer failed", hr);
    return false;
  }
  player_->SetVolume(static_cast<float>(state_.volume));
  player_->SetRate(static_cast<float>(state_.rate));
  return true;
}

void PlaybackController::HandleMediaEvent(MFP_EVENT_HEADER* event) {
  if (!event) {
    return;
  }

  IMFPMediaItem* mediaItem = nullptr;
  IMFPMediaPlayer* mediaPlayer = event->pMediaPlayer;
  bool shouldSetMediaItem = false;
  bool shouldPlay = false;

  {
    std::lock_guard lock(mutex_);
    if (FAILED(event->hrEvent)) {
      state_.kind = echo::core::PlaybackStateKind::Failed;
      state_.error = HResultMessage("Media Foundation playback event failed", event->hrEvent);
      return;
    }

    switch (event->eEventType) {
      case MFP_EVENT_TYPE_MEDIAITEM_CREATED: {
        auto* created = MFP_GET_MEDIAITEM_CREATED_EVENT(event);
        mediaItem = created ? created->pMediaItem : nullptr;
        shouldSetMediaItem = mediaItem != nullptr && mediaPlayer != nullptr;
        break;
      }
      case MFP_EVENT_TYPE_MEDIAITEM_SET:
        shouldPlay = mediaPlayer != nullptr;
        break;
      case MFP_EVENT_TYPE_PLAY:
        state_.kind = echo::core::PlaybackStateKind::Playing;
        state_.error.clear();
        break;
      case MFP_EVENT_TYPE_PAUSE:
        state_.kind = echo::core::PlaybackStateKind::Paused;
        break;
      case MFP_EVENT_TYPE_STOP:
        state_.kind = echo::core::PlaybackStateKind::Stopped;
        state_.currentSeconds = 0.0;
        break;
      case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        state_.kind = echo::core::PlaybackStateKind::Stopped;
        break;
      case MFP_EVENT_TYPE_POSITION_SET:
      case MFP_EVENT_TYPE_RATE_SET:
      case MFP_EVENT_TYPE_MF:
      case MFP_EVENT_TYPE_FRAME_STEP:
      case MFP_EVENT_TYPE_MEDIAITEM_CLEARED:
      case MFP_EVENT_TYPE_ACQUIRE_USER_CREDENTIAL:
        break;
      case MFP_EVENT_TYPE_ERROR:
        state_.kind = echo::core::PlaybackStateKind::Failed;
        state_.error = "Media Foundation playback failed";
        break;
    }
  }

  if (shouldSetMediaItem) {
    mediaPlayer->SetMediaItem(mediaItem);
  }
  if (shouldPlay) {
    mediaPlayer->Play();
  }
}

}  // namespace echo::playback

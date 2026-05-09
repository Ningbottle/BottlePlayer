#include "echo/playback/PlaybackController.h"

#include <mfapi.h>

#include <algorithm>

namespace echo::playback {

PlaybackController::PlaybackController() = default;

PlaybackController::~PlaybackController() {
  Stop();
  if (mediaFoundationStarted_) {
    MFShutdown();
  }
}

bool PlaybackController::Initialize() {
  std::lock_guard lock(mutex_);
  if (mediaFoundationStarted_) return true;
  const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.error = "MFStartup failed";
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
  if (url.empty()) {
    state_.kind = echo::core::PlaybackStateKind::Failed;
    state_.sourceUrl.clear();
    state_.error = "Playback URL is empty";
    return false;
  }
  state_.sourceUrl = url;
  state_.currentSeconds = 0.0;
  state_.durationSeconds = 0.0;
  // The media-session pipeline is the next implementation step; keeping this
  // state transition explicit lets the UI and tests wire against the final API.
  state_.kind = echo::core::PlaybackStateKind::Opening;
  state_.error.clear();
  return true;
}

void PlaybackController::Pause() {
  std::lock_guard lock(mutex_);
  if (state_.kind == echo::core::PlaybackStateKind::Playing ||
      state_.kind == echo::core::PlaybackStateKind::Opening) {
    state_.kind = echo::core::PlaybackStateKind::Paused;
  }
}

void PlaybackController::Resume() {
  std::lock_guard lock(mutex_);
  if (state_.kind == echo::core::PlaybackStateKind::Paused) {
    state_.kind = echo::core::PlaybackStateKind::Playing;
  }
}

void PlaybackController::Stop() {
  std::lock_guard lock(mutex_);
  if (state_.kind != echo::core::PlaybackStateKind::Idle) {
    state_.kind = echo::core::PlaybackStateKind::Stopped;
    state_.currentSeconds = 0.0;
  }
}

void PlaybackController::Seek(double seconds) {
  std::lock_guard lock(mutex_);
  state_.currentSeconds = std::max(0.0, seconds);
}

void PlaybackController::SetVolume(double volume) {
  std::lock_guard lock(mutex_);
  state_.volume = std::clamp(volume, 0.0, 1.0);
}

void PlaybackController::SetRate(double rate) {
  std::lock_guard lock(mutex_);
  state_.rate = std::clamp(rate, 0.5, 2.0);
}

echo::core::PlaybackState PlaybackController::GetState() const {
  std::lock_guard lock(mutex_);
  return state_;
}

}  // namespace echo::playback

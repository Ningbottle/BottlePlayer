#include "echo/playback/PlaybackController.h"

#include "echo/playback/PlaybackControllerImpl.h"

namespace echo::playback {

// Forward-declared factories defined in PlaybackControllerMFP.cpp and
// PlaybackControllerMFS.cpp respectively. Pimpl picks the right one based
// on the Backend enum.
std::unique_ptr<PlaybackControllerImpl> CreateMfpImpl();
std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl();

PlaybackController::PlaybackController() = default;

PlaybackController::~PlaybackController() = default;

bool PlaybackController::Initialize(Backend backend) {
  if (impl_) return true;  // already initialized
  switch (backend) {
    case Backend::MFP: impl_ = CreateMfpImpl(); break;
    case Backend::MFS: impl_ = CreateMfsImpl(); break;
  }
  if (!impl_) return false;
  return impl_->Initialize();
}

bool PlaybackController::PlayUrl(const std::string& url) {
  return impl_ ? impl_->PlayUrl(url) : false;
}

void PlaybackController::Pause() { if (impl_) impl_->Pause(); }
void PlaybackController::Resume() { if (impl_) impl_->Resume(); }
void PlaybackController::Stop() { if (impl_) impl_->Stop(); }
void PlaybackController::Seek(double s) { if (impl_) impl_->Seek(s); }
void PlaybackController::SetVolume(double v) { if (impl_) impl_->SetVolume(v); }
void PlaybackController::SetRate(double r) { if (impl_) impl_->SetRate(r); }
echo::core::PlaybackState PlaybackController::GetState() const {
  return impl_ ? impl_->GetState() : echo::core::PlaybackState{};
}

void PlaybackController::SetEqEnabled(bool enabled) { if (impl_) impl_->SetEqEnabled(enabled); }
void PlaybackController::SetEqBand(int idx, double gainDb) { if (impl_) impl_->SetEqBand(idx, gainDb); }
void PlaybackController::SetEqBands(const double gainsDb[5]) { if (impl_) impl_->SetEqBands(gainsDb); }
void PlaybackController::GetEqBands(double out[5]) const { if (impl_) impl_->GetEqBands(out); }

void PlaybackController::SetEventCallback(EventCallback cb, void* userData) {
  if (impl_) impl_->SetEventCallback(cb, userData);
}

}  // namespace echo::playback

#include "echo/playback/PlaybackControllerMFS.h"

#include <objbase.h>
#include <mfapi.h>

#include "echo/diagnostics/EchoDiagnostics.h"

namespace echo::playback {

PlaybackControllerMFS::PlaybackControllerMFS() = default;

PlaybackControllerMFS::~PlaybackControllerMFS() {
  if (mfStarted_) MFShutdown();
  if (comInitialized_) CoUninitialize();
}

bool PlaybackControllerMFS::Initialize() {
  std::lock_guard lock(mutex_);
  if (mfStarted_) return true;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    ECHO_LOG("PlaybackMFS", "CoInitializeEx failed");
    return false;
  }
  comInitialized_ = true;
  hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "MFStartup failed");
    return false;
  }
  mfStarted_ = true;
  return true;
}

bool PlaybackControllerMFS::PlayUrl(const std::string&) { return false; }
void PlaybackControllerMFS::Pause() {}
void PlaybackControllerMFS::Resume() {}
void PlaybackControllerMFS::Stop() {}
void PlaybackControllerMFS::Seek(double) {}
void PlaybackControllerMFS::SetVolume(double) {}
void PlaybackControllerMFS::SetRate(double) {}
echo::core::PlaybackState PlaybackControllerMFS::GetState() const { return state_; }

std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl() {
  return std::make_unique<PlaybackControllerMFS>();
}

}  // namespace echo::playback

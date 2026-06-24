#pragma once
#include "echo/playback/PlaybackControllerImpl.h"
#include "echo/playback/EqualizerMFT.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "echo/core/Dto.h"

namespace echo::playback {

class MfsEventCallback;

class PlaybackControllerMFS final : public PlaybackControllerImpl {
  friend class MfsEventCallback;

 public:
  PlaybackControllerMFS();
  ~PlaybackControllerMFS() override;

  bool Initialize() override;
  bool PlayUrl(const std::string& url) override;
  void Pause() override;
  void Resume() override;
  void Stop() override;
  void Seek(double seconds) override;
  void SetVolume(double volume) override;
  void SetRate(double rate) override;
  echo::core::PlaybackState GetState() const override;
  void SetEventCallback(PlaybackController::EventCallback cb,
                        void* userData) override;

 private:
  bool comInitialized_ = false;
  bool mfStarted_ = false;
  mutable std::mutex mutex_;
  echo::core::PlaybackState state_;

  IMFMediaSession* session_ = nullptr;
  IMFMediaSource* mediaSource_ = nullptr;
  IMFTopology* topology_ = nullptr;
  MfsEventCallback* eventCallback_ = nullptr;

  IMFSimpleAudioVolume* audioVolume_ = nullptr;
  IMFRateControl* rateControl_ = nullptr;
  IMFPresentationClock* clock_ = nullptr;
  EqualizerMFT* eqMft_ = nullptr;

  std::thread positionThread_;
  std::atomic<bool> positionStop_{false};
  PlaybackController::EventCallback eventCb_ = nullptr;
  void* eventUserData_ = nullptr;
  double duration_ = 0.0;

  HRESULT BuildTopology(const std::string& url, IMFTopology** out);
  void OnSessionEvent(MediaEventType metype);
  void EmitEvent(const char* type, double position, double duration,
                 const char* state);
  void PositionPollLoop();
};

std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl();

}  // namespace echo::playback

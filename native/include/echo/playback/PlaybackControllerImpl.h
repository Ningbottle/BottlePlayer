#pragma once
#include <string>
#include "echo/core/Dto.h"
#include "echo/playback/PlaybackController.h"  // for EventCallback typedef

namespace echo::playback {

class PlaybackControllerImpl {
 public:
  virtual ~PlaybackControllerImpl() = default;
  virtual bool Initialize() = 0;
  virtual bool PlayUrl(const std::string& url) = 0;
  virtual void Pause() = 0;
  virtual void Resume() = 0;
  virtual void Stop() = 0;
  virtual void Seek(double seconds) = 0;
  virtual void SetVolume(double volume) = 0;
  virtual void SetRate(double rate) = 0;
  virtual echo::core::PlaybackState GetState() const = 0;

  // EQ default no-op (MFP backend does not implement EQ)
  virtual void SetEqEnabled(bool) {}
  virtual void SetEqBand(int /*bandIndex*/, double /*gainDb*/) {}
  virtual void SetEqBands(const double /*gainsDb*/[5]) {}
  virtual void GetEqBands(double out[5]) const {
    for (int i = 0; i < 5; ++i) out[i] = 0.0;
  }

  virtual void SetEventCallback(PlaybackController::EventCallback /*cb*/,
                                void* /*userData*/) {}
};

}  // namespace echo::playback

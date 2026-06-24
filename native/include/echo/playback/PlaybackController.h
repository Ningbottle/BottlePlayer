#pragma once

#include <memory>
#include <string>

#include "echo/core/Dto.h"

namespace echo::playback {

class PlaybackControllerImpl;

class PlaybackController {
 public:
  using EventCallback = void (*)(const char* jsonPayload, void* userData);

  enum class Backend { MFP, MFS };

  PlaybackController();
  ~PlaybackController();

  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  bool Initialize(Backend backend = Backend::MFP);
  bool PlayUrl(const std::string& url);
  void Pause();
  void Resume();
  void Stop();
  void Seek(double seconds);
  void SetVolume(double volume);
  void SetRate(double rate);

  echo::core::PlaybackState GetState() const;

  // New methods (S4)
  void SetEqEnabled(bool enabled);
  void SetEqBand(int bandIndex, double gainDb);
  void SetEqBands(const double gainsDb[5]);
  void GetEqBands(double outGainsDb[5]) const;
  void SetEventCallback(EventCallback cb, void* userData);

 private:
  std::unique_ptr<PlaybackControllerImpl> impl_;
};

}  // namespace echo::playback

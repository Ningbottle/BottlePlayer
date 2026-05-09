#pragma once

#include <mutex>
#include <string>

#include "echo/core/Dto.h"

namespace echo::playback {

class PlaybackController {
 public:
  PlaybackController();
  ~PlaybackController();

  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  bool Initialize();
  bool PlayUrl(const std::string& url);
  void Pause();
  void Resume();
  void Stop();
  void Seek(double seconds);
  void SetVolume(double volume);
  void SetRate(double rate);

  echo::core::PlaybackState GetState() const;

 private:
  mutable std::mutex mutex_;
  echo::core::PlaybackState state_;
  bool mediaFoundationStarted_ = false;
};

}  // namespace echo::playback


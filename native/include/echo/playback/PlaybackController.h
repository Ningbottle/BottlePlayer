#pragma once

#include <mutex>
#include <string>

#include "echo/core/Dto.h"

struct IMFPMediaPlayer;
struct MFP_EVENT_HEADER;

namespace echo::playback {

class MediaPlayerCallback;

class PlaybackController {
 public:
  using EventCallback = void (*)(const char* jsonPayload, void* userData);

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
  friend class MediaPlayerCallback;

  void ReleasePlayerLocked();
  bool EnsurePlayerLocked();
  void HandleMediaEvent(MFP_EVENT_HEADER* event);

  mutable std::mutex mutex_;
  echo::core::PlaybackState state_;
  bool mediaFoundationStarted_ = false;
  bool comInitialized_ = false;
  IMFPMediaPlayer* player_ = nullptr;
  MediaPlayerCallback* callback_ = nullptr;
};

}  // namespace echo::playback

#pragma once
#include "echo/playback/PlaybackControllerImpl.h"

#include <memory>
#include <mutex>
#include <string>

#include "echo/core/Dto.h"

namespace echo::playback {

class PlaybackControllerMFS final : public PlaybackControllerImpl {
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

 private:
  bool comInitialized_ = false;
  bool mfStarted_ = false;
  mutable std::mutex mutex_;
  echo::core::PlaybackState state_;
};

std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl();

}  // namespace echo::playback

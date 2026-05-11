#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace echo::win32_app {

struct QueueTrack {
  std::wstring title;
  std::wstring artist;
  std::wstring album;
  std::wstring duration;
};

class PlaybackQueueState {
 public:
  explicit PlaybackQueueState(std::vector<QueueTrack> tracks = {});

  bool HasTracks() const;
  std::size_t CurrentIndex() const;
  const QueueTrack* Current() const;

  const std::vector<QueueTrack>& Tracks() const;
  const QueueTrack* Next();
  const QueueTrack* Previous();
  const QueueTrack* Select(std::size_t index);

 private:
  std::vector<QueueTrack> tracks_;
  std::size_t currentIndex_ = 0;
};

}  // namespace echo::win32_app

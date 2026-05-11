#include "echo/win32_app/PlaybackQueue.h"

#include <utility>

namespace echo::win32_app {

PlaybackQueueState::PlaybackQueueState(std::vector<QueueTrack> tracks) : tracks_(std::move(tracks)) {}

bool PlaybackQueueState::HasTracks() const {
  return !tracks_.empty();
}

std::size_t PlaybackQueueState::CurrentIndex() const {
  return currentIndex_;
}

const QueueTrack* PlaybackQueueState::Current() const {
  if (tracks_.empty()) {
    return nullptr;
  }
  return &tracks_[currentIndex_];
}

const std::vector<QueueTrack>& PlaybackQueueState::Tracks() const {
  return tracks_;
}

const QueueTrack* PlaybackQueueState::Next() {
  if (tracks_.empty()) {
    return nullptr;
  }
  currentIndex_ = (currentIndex_ + 1) % tracks_.size();
  return Current();
}

const QueueTrack* PlaybackQueueState::Previous() {
  if (tracks_.empty()) {
    return nullptr;
  }
  currentIndex_ = currentIndex_ == 0 ? tracks_.size() - 1 : currentIndex_ - 1;
  return Current();
}

const QueueTrack* PlaybackQueueState::Select(std::size_t index) {
  if (index >= tracks_.size()) {
    return nullptr;
  }
  currentIndex_ = index;
  return Current();
}

}  // namespace echo::win32_app

#include "echo/async/EventQueue.h"

#include <utility>

namespace echo::async {

void EventQueue::Push(BackendEvent event) {
  std::lock_guard lock(mutex_);
  events_.push(std::move(event));
}

std::optional<BackendEvent> EventQueue::TryPop() {
  std::lock_guard lock(mutex_);
  if (events_.empty()) {
    return std::nullopt;
  }

  auto event = std::move(events_.front());
  events_.pop();
  return event;
}

void EventQueue::Clear() {
  std::lock_guard lock(mutex_);
  events_ = {};
}

std::size_t EventQueue::Size() const {
  std::lock_guard lock(mutex_);
  return events_.size();
}

}  // namespace echo::async

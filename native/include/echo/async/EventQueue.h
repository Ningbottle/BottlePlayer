#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>

namespace echo::async {

struct BackendEvent {
  std::string type;
  std::string payload;
};

class EventQueue {
 public:
  void Push(BackendEvent event);
  std::optional<BackendEvent> TryPop();
  void Clear();
  std::size_t Size() const;

 private:
  mutable std::mutex mutex_;
  std::queue<BackendEvent> events_;
};

}  // namespace echo::async

#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>

#include "echo/async/EventQueue.h"

namespace echo::async {

class CancellationToken {
 public:
  bool IsCancellationRequested() const;

 private:
  friend class CancellationSource;
  explicit CancellationToken(std::shared_ptr<std::atomic_bool> cancelled);

  std::shared_ptr<std::atomic_bool> cancelled_;
};

class CancellationSource {
 public:
  CancellationSource();

  CancellationToken Token() const;
  void Cancel();

 private:
  std::shared_ptr<std::atomic_bool> cancelled_;
};

class TaskScheduler {
 public:
  explicit TaskScheduler(std::size_t workerCount = 1);
  ~TaskScheduler();

  TaskScheduler(const TaskScheduler&) = delete;
  TaskScheduler& operator=(const TaskScheduler&) = delete;

  std::future<void> Schedule(std::function<void(CancellationToken)> work,
                             CancellationToken token);
  std::future<void> ScheduleAndPost(std::function<BackendEvent(CancellationToken)> work,
                                    CancellationToken token,
                                    EventQueue& events);
  std::size_t PendingCount() const;
  void Shutdown();

 private:
  std::size_t workerCount_;
  std::atomic_size_t pending_{0};
  std::atomic_bool shutdown_{false};
};

}  // namespace echo::async

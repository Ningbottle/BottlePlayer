#include "echo/async/TaskScheduler.h"

#include <stdexcept>
#include <utility>

namespace echo::async {

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> cancelled)
    : cancelled_(std::move(cancelled)) {}

bool CancellationToken::IsCancellationRequested() const {
  return cancelled_ && cancelled_->load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : cancelled_(std::make_shared<std::atomic_bool>(false)) {}

CancellationToken CancellationSource::Token() const {
  return CancellationToken(cancelled_);
}

void CancellationSource::Cancel() {
  cancelled_->store(true, std::memory_order_release);
}

TaskScheduler::TaskScheduler(std::size_t workerCount)
    : workerCount_(workerCount == 0 ? 1 : workerCount) {}

TaskScheduler::~TaskScheduler() {
  Shutdown();
}

std::future<void> TaskScheduler::Schedule(std::function<void(CancellationToken)> work,
                                          CancellationToken token) {
  if (shutdown_.load(std::memory_order_acquire)) {
    throw std::runtime_error("TaskScheduler has been shut down");
  }

  (void)workerCount_;
  pending_.fetch_add(1, std::memory_order_acq_rel);
  return std::async(std::launch::async, [this, work = std::move(work), token = std::move(token)]() mutable {
    struct PendingGuard {
      std::atomic_size_t& pending;
      ~PendingGuard() { pending.fetch_sub(1, std::memory_order_acq_rel); }
    } guard{pending_};

    if (!token.IsCancellationRequested()) {
      work(token);
    }
  });
}

std::future<void> TaskScheduler::ScheduleAndPost(std::function<BackendEvent(CancellationToken)> work,
                                                 CancellationToken token,
                                                 EventQueue& events) {
  return Schedule(
      [&events, work = std::move(work)](CancellationToken token) mutable {
        auto event = work(token);
        if (!token.IsCancellationRequested()) {
          events.Push(std::move(event));
        }
      },
      std::move(token));
}

std::size_t TaskScheduler::PendingCount() const {
  return pending_.load(std::memory_order_acquire);
}

void TaskScheduler::Shutdown() {
  shutdown_.store(true, std::memory_order_release);
}

}  // namespace echo::async

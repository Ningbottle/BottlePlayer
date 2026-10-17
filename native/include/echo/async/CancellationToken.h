#pragma once

#include <atomic>
#include <memory>

namespace echo::async {

class CancellationToken {
 public:
  bool IsCancellationRequested() const;
  const std::atomic_bool* Flag() const { return cancelled_.get(); }
  std::shared_ptr<const std::atomic_bool> SharedFlag() const { return cancelled_; }

 private:
  friend class CancellationSource;
  friend class RequestScheduler;
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

}  // namespace echo::async

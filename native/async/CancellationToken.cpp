#include "echo/async/CancellationToken.h"

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

}  // namespace echo::async

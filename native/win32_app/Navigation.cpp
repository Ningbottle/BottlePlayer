#include "echo/win32_app/Navigation.h"

#include <algorithm>
#include <cstddef>

namespace echo::win32_app {

NavigationState::NavigationState(PageId initial) : history_{initial}, index_(0) {}

PageId NavigationState::Current() const {
  return history_[index_];
}

bool NavigationState::CanGoBack() const {
  return index_ > 0;
}

bool NavigationState::CanGoForward() const {
  return index_ + 1 < history_.size();
}

void NavigationState::NavigateTo(PageId page) {
  if (Current() == page) {
    return;
  }

  history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(index_ + 1), history_.end());
  history_.push_back(page);
  index_ = history_.size() - 1;
}

bool NavigationState::GoBack() {
  if (!CanGoBack()) {
    return false;
  }
  --index_;
  return true;
}

bool NavigationState::GoForward() {
  if (!CanGoForward()) {
    return false;
  }
  ++index_;
  return true;
}

}  // namespace echo::win32_app

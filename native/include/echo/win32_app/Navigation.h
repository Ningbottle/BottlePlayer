#pragma once

#include <vector>

namespace echo::win32_app {

enum class PageId {
  Home,
  NowPlaying,
  Search,
  Settings,
};

class NavigationState {
 public:
  explicit NavigationState(PageId initial = PageId::Home);

  PageId Current() const;
  bool CanGoBack() const;
  bool CanGoForward() const;

  void NavigateTo(PageId page);
  bool GoBack();
  bool GoForward();

 private:
  std::vector<PageId> history_;
  std::size_t index_ = 0;
};

}  // namespace echo::win32_app

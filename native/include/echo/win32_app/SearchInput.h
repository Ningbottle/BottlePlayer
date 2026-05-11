#pragma once

#include <string>

namespace echo::win32_app {

enum class SearchInputAction {
  None,
  Submit,
};

struct SearchInputResult {
  SearchInputAction action = SearchInputAction::None;
  std::wstring submittedText;
};

class SearchInputState {
 public:
  void Focus();
  void Blur();
  bool IsFocused() const;

  const std::wstring& Text() const;
  void SetText(std::wstring text);
  SearchInputResult HandleCharacter(wchar_t value);

 private:
  bool focused_ = false;
  std::wstring text_;
};

}  // namespace echo::win32_app

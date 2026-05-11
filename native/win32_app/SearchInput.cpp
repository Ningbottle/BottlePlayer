#include "echo/win32_app/SearchInput.h"

#include <algorithm>

namespace echo::win32_app {
namespace {

std::wstring TrimCopy(const std::wstring& value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
  }).base();
  if (first >= last) {
    return {};
  }
  return std::wstring(first, last);
}

}  // namespace

void SearchInputState::Focus() {
  focused_ = true;
}

void SearchInputState::Blur() {
  focused_ = false;
}

bool SearchInputState::IsFocused() const {
  return focused_;
}

const std::wstring& SearchInputState::Text() const {
  return text_;
}

void SearchInputState::SetText(std::wstring text) {
  constexpr std::size_t kMaxLength = 80;
  if (text.size() > kMaxLength) {
    text.resize(kMaxLength);
  }
  text_ = std::move(text);
}

SearchInputResult SearchInputState::HandleCharacter(wchar_t value) {
  if (!focused_) {
    return {};
  }

  if (value == L'\b') {
    if (!text_.empty()) {
      text_.pop_back();
    }
    return {};
  }

  if (value == L'\r' || value == L'\n') {
    auto submitted = TrimCopy(text_);
    if (submitted.empty()) {
      return {};
    }
    return {SearchInputAction::Submit, std::move(submitted)};
  }

  if (value < 32) {
    return {};
  }

  constexpr std::size_t kMaxLength = 80;
  if (text_.size() < kMaxLength) {
    text_.push_back(value);
  }
  return {};
}

}  // namespace echo::win32_app

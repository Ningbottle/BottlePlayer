#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace echo::win32_app {

enum class SearchState {
  Loading,
  Ready,
  Empty,
  Error,
};

struct SearchResultRow {
  std::wstring title;
  std::wstring artist;
  std::wstring album;
  std::wstring duration;
  std::string hash;
};

struct SearchViewModel {
  std::wstring keyword;
  SearchState state = SearchState::Loading;
  std::wstring message;
  std::size_t total = 0;
  std::vector<SearchResultRow> rows;
};

SearchViewModel BuildSearchViewModel(const std::string& keyword, const nlohmann::json& response);

}  // namespace echo::win32_app

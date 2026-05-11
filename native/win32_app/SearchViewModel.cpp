#include "echo/win32_app/SearchViewModel.h"

#include <iomanip>
#include <sstream>
#include <string_view>

#include <windows.h>

namespace echo::win32_app {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), size);
  return wide;
}

std::string JsonString(const nlohmann::json& value, std::string_view key) {
  const auto found = value.find(std::string(key));
  return found != value.end() && found->is_string() ? found->get<std::string>() : "";
}

int JsonInt(const nlohmann::json& value, std::string_view key) {
  const auto found = value.find(std::string(key));
  if (found == value.end()) {
    return 0;
  }
  if (found->is_number_integer()) {
    return found->get<int>();
  }
  if (found->is_string()) {
    try {
      return std::stoi(found->get<std::string>());
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

std::wstring FormatDuration(int seconds) {
  if (seconds <= 0) {
    return L"--:--";
  }

  std::wostringstream stream;
  stream << std::setw(2) << std::setfill(L'0') << seconds / 60
         << L":"
         << std::setw(2) << std::setfill(L'0') << seconds % 60;
  return stream.str();
}

std::string FirstNonEmpty(std::initializer_list<std::string> values) {
  for (auto& value : values) {
    if (!value.empty()) {
      return value;
    }
  }
  return {};
}

const nlohmann::json* FindRows(const nlohmann::json& response) {
  if (!response.contains("data") || !response["data"].is_object()) {
    return nullptr;
  }

  const auto& data = response["data"];
  if (data.contains("lists") && data["lists"].is_array()) {
    return &data["lists"];
  }
  if (data.contains("info") && data["info"].is_array()) {
    return &data["info"];
  }
  return nullptr;
}

}  // namespace

SearchViewModel BuildSearchViewModel(const std::string& keyword, const nlohmann::json& response) {
  SearchViewModel viewModel;
  viewModel.keyword = Utf8ToWide(keyword);

  if (response.value("status", 0) != 1) {
    viewModel.state = SearchState::Error;
    viewModel.message = Utf8ToWide(response.value("error", "搜索失败"));
    return viewModel;
  }

  const auto* rows = FindRows(response);
  if (!rows || rows->empty()) {
    viewModel.state = SearchState::Empty;
    viewModel.message = L"没有找到相关歌曲";
    return viewModel;
  }

  viewModel.state = SearchState::Ready;
  viewModel.total = response["data"].value("total", rows->size());
  viewModel.rows.reserve(rows->size());

  for (const auto& row : *rows) {
    SearchResultRow item;
    item.title = Utf8ToWide(JsonString(row, "SongName").empty() ? JsonString(row, "songname") : JsonString(row, "SongName"));
    item.artist = Utf8ToWide(JsonString(row, "SingerName").empty() ? JsonString(row, "singername") : JsonString(row, "SingerName"));
    item.album = Utf8ToWide(JsonString(row, "AlbumName").empty() ? JsonString(row, "album_name") : JsonString(row, "AlbumName"));
    item.duration = FormatDuration(JsonInt(row, "Duration") == 0 ? JsonInt(row, "duration") : JsonInt(row, "Duration"));
    item.hash = JsonString(row, "FileHash").empty() ? JsonString(row, "hash") : JsonString(row, "FileHash");
    item.coverUrl = FirstNonEmpty({
        JsonString(row, "Image"),
        JsonString(row, "imgurl"),
        JsonString(row, "cover"),
        JsonString(row, "pic_url"),
        JsonString(row, "sizable_cover"),
    });
    if (!item.coverUrl.empty()) {
      item.imageKey = "remote-cover:" + item.coverUrl;
    }
    viewModel.rows.push_back(std::move(item));
  }

  if (viewModel.rows.empty()) {
    viewModel.state = SearchState::Empty;
    viewModel.message = L"没有找到相关歌曲";
  }

  return viewModel;
}

}  // namespace echo::win32_app

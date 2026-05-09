#include "echo/core/SearchService.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace echo::core {
namespace {

int Clamp(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  if (first > 0) value.erase(0, first);
  return value;
}

std::string UrlEncode(std::string_view value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      stream << static_cast<char>(ch);
    } else {
      stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return stream.str();
}

nlohmann::json EmptySearchPage(std::string type = "song", std::string keywords = "") {
  return {
      {"status", 1},
      {"data",
       {
           {"lists", nlohmann::json::array()},
           {"list", nlohmann::json::array()},
           {"info", nlohmann::json::array()},
           {"total", 0},
           {"type", std::move(type)},
           {"keywords", std::move(keywords)},
       }},
  };
}

std::string ReadString(const nlohmann::json& value, std::string_view key) {
  if (!value.contains(key)) return "";
  const auto& item = value.at(key);
  if (item.is_string()) return item.get<std::string>();
  if (item.is_number_integer()) return std::to_string(item.get<std::int64_t>());
  if (item.is_number_unsigned()) return std::to_string(item.get<std::uint64_t>());
  if (item.is_number_float()) return std::to_string(item.get<double>());
  return "";
}

int ReadInt(const nlohmann::json& value, std::string_view key, int fallback = 0) {
  if (!value.contains(key)) return fallback;
  const auto& item = value.at(key);
  if (item.is_number_integer()) return item.get<int>();
  if (item.is_number_unsigned()) return static_cast<int>(item.get<unsigned int>());
  if (item.is_string()) {
    try {
      return std::stoi(item.get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

nlohmann::json NormalizeSongResult(const nlohmann::json& raw) {
  const auto transParam = raw.value("trans_param", nlohmann::json::object());
  const auto singerName = ReadString(raw, "singername");
  const auto hash = ReadString(raw, "hash");
  const auto mixSongId = ReadInt(raw, "album_audio_id");
  const auto audioId = ReadInt(raw, "audio_id");

  nlohmann::json item = raw;
  item["FileHash"] = hash;
  item["SongName"] = ReadString(raw, "songname");
  item["FileName"] = ReadString(raw, "filename");
  item["SingerName"] = singerName;
  item["AlbumName"] = ReadString(raw, "album_name");
  item["AlbumID"] = ReadString(raw, "album_id");
  item["Duration"] = ReadInt(raw, "duration");
  item["MixSongID"] = mixSongId;
  item["Audioid"] = audioId;
  item["MVHash"] = ReadString(raw, "mvhash");
  item["AlbumPrivilege"] = ReadInt(raw, "privilege");
  item["OldCpy"] = ReadInt(raw, "old_cpy");
  item["PayType"] = ReadInt(raw, "pay_type");
  item["Image"] = transParam.value("union_cover", "");

  item["HQ"] = {{"Hash", ReadString(raw, "320hash")}};
  item["SQ"] = {{"Hash", ReadString(raw, "sqhash")}};
  item["Res"] = {{"Hash", ReadString(raw, "reshash")}};
  item["Singers"] = nlohmann::json::array();
  if (!singerName.empty()) {
    item["Singers"].push_back({{"name", singerName}, {"id", ""}});
  }

  return item;
}

nlohmann::json NormalizeSpecialResult(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = ReadInt(raw, "specialid", ReadInt(raw, "id"));
  const auto name = ReadString(raw, "specialname").empty() ? ReadString(raw, "name") : ReadString(raw, "specialname");
  const auto image = ReadString(raw, "imgurl").empty() ? ReadString(raw, "cover") : ReadString(raw, "imgurl");

  item["specialid"] = id;
  item["id"] = id;
  item["listid"] = id;
  item["specialname"] = name;
  item["name"] = name;
  item["title"] = name;
  item["imgurl"] = image;
  item["cover"] = image;
  item["pic_url"] = image;
  item["playcount"] = ReadInt(raw, "playcount");
  item["songcount"] = ReadInt(raw, "songcount");
  item["nickname"] = ReadString(raw, "nickname").empty() ? ReadString(raw, "username") : ReadString(raw, "nickname");
  item["username"] = ReadString(raw, "username").empty() ? item["nickname"].get<std::string>() : ReadString(raw, "username");
  item["publishtime"] = ReadString(raw, "publishtime");
  item["intro"] = ReadString(raw, "intro");
  return item;
}

nlohmann::json NormalizeAlbumResult(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = ReadInt(raw, "albumid", ReadInt(raw, "album_id", ReadInt(raw, "AlbumId")));
  const auto name = ReadString(raw, "albumname").empty() ? ReadString(raw, "album_name") : ReadString(raw, "albumname");
  const auto singer = ReadString(raw, "singername").empty() ? ReadString(raw, "singer_name") : ReadString(raw, "singername");
  const auto singerId = ReadInt(raw, "singerid", ReadInt(raw, "singer_id"));
  const auto image = ReadString(raw, "imgurl").empty() ? ReadString(raw, "sizable_cover") : ReadString(raw, "imgurl");

  item["AlbumId"] = id;
  item["AlbumName"] = name;
  item["SingerName"] = singer;
  item["SingerId"] = singerId;
  item["albumid"] = id;
  item["album_id"] = id;
  item["albumname"] = name;
  item["album_name"] = name;
  item["name"] = name;
  item["singername"] = singer;
  item["singerid"] = singerId;
  item["imgurl"] = image;
  item["sizable_cover"] = image;
  item["songcount"] = ReadInt(raw, "songcount", ReadInt(raw, "song_count"));
  item["publishtime"] = ReadString(raw, "publishtime").empty() ? ReadString(raw, "publish_time") : ReadString(raw, "publishtime");
  item["intro"] = ReadString(raw, "intro");
  return item;
}

nlohmann::json NormalizeArtistResult(const nlohmann::json& raw) {
  nlohmann::json item = raw;
  const auto id = ReadInt(raw, "singerid", ReadInt(raw, "author_id", ReadInt(raw, "id")));
  const auto name = ReadString(raw, "singername").empty() ? ReadString(raw, "author_name") : ReadString(raw, "singername");
  const auto image = ReadString(raw, "imgurl").empty() ? ReadString(raw, "avatar") : ReadString(raw, "imgurl");

  item["AuthorId"] = id;
  item["AuthorName"] = name;
  item["singerid"] = id;
  item["author_id"] = id;
  item["id"] = id;
  item["singername"] = name;
  item["author_name"] = name;
  item["name"] = name;
  item["imgurl"] = image;
  item["avatar"] = image;
  item["songcount"] = ReadInt(raw, "songcount", ReadInt(raw, "song_count"));
  item["albumcount"] = ReadInt(raw, "albumcount", ReadInt(raw, "album_count"));
  item["mvcount"] = ReadInt(raw, "mvcount", ReadInt(raw, "mv_count"));
  return item;
}

nlohmann::json ErrorSearchPage(
    const std::string& keywords,
    const std::string& message,
    long upstreamStatus = 0) {
  auto body = EmptySearchPage("song", keywords);
  body["status"] = 0;
  body["error_code"] = "native_search_failed";
  body["error"] = message;
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

nlohmann::json ErrorHotSearch(const std::string& message, long upstreamStatus = 0) {
  nlohmann::json body = {
      {"status", 0},
      {"error_code", "native_search_hot_failed"},
      {"error", message},
      {"data", {{"list", nlohmann::json::array()}, {"info", nlohmann::json::array()}, {"total", 0}}},
  };
  if (upstreamStatus > 0) body["upstream_status"] = upstreamStatus;
  return body;
}

std::string BestHintInfo(const nlohmann::json& song) {
  const auto fileName = ReadString(song, "FileName");
  if (!fileName.empty()) return fileName;

  const auto singer = ReadString(song, "SingerName");
  const auto songName = ReadString(song, "SongName");
  if (!singer.empty() && !songName.empty()) return singer + " - " + songName;
  if (!songName.empty()) return songName;
  return ReadString(song, "filename");
}

}  // namespace

SearchService::SearchService()
    : SearchService([](
          const std::string& url,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Get(url, headers);
      }) {}

SearchService::SearchService(SearchHttpGet httpGet) : httpGet_(std::move(httpGet)) {}

nlohmann::json SearchService::Search(
    std::string keywords,
    std::string type,
    int page,
    int pageSize) const {
  keywords = Trim(std::move(keywords));
  type = Trim(std::move(type));
  if (type.empty()) type = "song";

  page = Clamp(page, 1, 1000);
  pageSize = Clamp(pageSize, 1, 100);

  if (keywords.empty()) {
    return EmptySearchPage(type, keywords);
  }

  std::string endpoint = "song";
  if (type == "special") {
    endpoint = "special";
  } else if (type == "album") {
    endpoint = "album";
  } else if (type == "author" || type == "artist" || type == "singer") {
    endpoint = "singer";
    type = "author";
  } else if (type != "song") {
    return EmptySearchPage(type, keywords);
  }

  const auto url =
      "http://mobilecdn.kugou.com/api/v3/search/" + endpoint +
      "?format=json&showtype=1&keyword=" +
      UrlEncode(keywords) + "&page=" + std::to_string(page) +
      "&pagesize=" + std::to_string(pageSize);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) {
    return ErrorSearchPage(keywords, result.error, result.statusCode);
  }

  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorSearchPage(keywords, "Kugou search upstream returned an error", result.statusCode);
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return ErrorSearchPage(keywords, std::string("Invalid Kugou search JSON: ") + error.what());
  }

  const auto data = upstream.value("data", type == "author" ? nlohmann::json::array() : nlohmann::json::object());
  const auto info = data.is_array() ? data : data.value("info", nlohmann::json::array());
  nlohmann::json list = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (!raw.is_object()) continue;
      if (type == "song") {
        list.push_back(NormalizeSongResult(raw));
      } else if (type == "special") {
        list.push_back(NormalizeSpecialResult(raw));
      } else if (type == "album") {
        list.push_back(NormalizeAlbumResult(raw));
      } else if (type == "author") {
        list.push_back(NormalizeArtistResult(raw));
      }
    }
  }

  const auto total = data.is_object() ? data.value("total", static_cast<int>(list.size())) : static_cast<int>(list.size());
  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data",
       {
           {"lists", list},
           {"list", list},
           {"info", list},
           {"total", total},
           {"page", page},
           {"pagesize", pageSize},
           {"type", type},
           {"keywords", keywords},
       }},
  };
}

nlohmann::json SearchService::Hot(int count) const {
  count = Clamp(count, 1, 50);
  const auto url =
      "http://mobilecdn.kugou.com/api/v3/search/hot?plat=0&count=" + std::to_string(count);

  const auto result = httpGet_(
      url,
      {
          {"Accept", "application/json"},
          {"User-Agent", "EchoMusicNative/0.1"},
      });

  if (!result.error.empty()) return ErrorHotSearch(result.error, result.statusCode);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return ErrorHotSearch("Kugou hot search upstream returned an error", result.statusCode);
  }

  nlohmann::json upstream;
  try {
    upstream = nlohmann::json::parse(result.body);
  } catch (const nlohmann::json::exception& error) {
    return ErrorHotSearch(std::string("Invalid Kugou hot search JSON: ") + error.what());
  }

  const auto data = upstream.value("data", nlohmann::json::object());
  const auto info = data.value("info", nlohmann::json::array());
  nlohmann::json keywords = nlohmann::json::array();
  if (info.is_array()) {
    for (const auto& raw : info) {
      if (!raw.is_object()) continue;
      keywords.push_back({
          {"keyword", ReadString(raw, "keyword")},
          {"reason", ReadString(raw, "reason").empty() ? ReadString(raw, "sort") : ReadString(raw, "reason")},
          {"sort", ReadInt(raw, "sort")},
          {"jumpurl", ReadString(raw, "jumpurl")},
      });
    }
  }

  return {
      {"status", 1},
      {"error", upstream.value("error", "")},
      {"data",
       {
           {"list", {{{"name", "热门搜索"}, {"keywords", keywords}}}},
           {"info", keywords},
           {"total", static_cast<int>(keywords.size())},
       }},
  };
}

nlohmann::json SearchService::Suggest(std::string keywords, int count) const {
  keywords = Trim(std::move(keywords));
  count = Clamp(count, 1, 20);
  if (keywords.empty()) {
    return {{"status", 1}, {"data", nlohmann::json::array()}};
  }

  const auto search = Search(keywords, "song", 1, count);
  if (search.value("status", 0) != 1) {
    return {
        {"status", 0},
        {"error_code", "native_search_suggest_failed"},
        {"error", search.value("error", "Kugou suggest fallback search failed")},
        {"data", nlohmann::json::array()},
      };
  }

  const auto data = search.value("data", nlohmann::json::object());
  const auto songs = data.value("info", nlohmann::json::array());
  nlohmann::json records = nlohmann::json::array();
  if (songs.is_array()) {
    for (const auto& song : songs) {
      if (!song.is_object()) continue;
      records.push_back({
          {"HintInfo", BestHintInfo(song)},
          {"SongName", ReadString(song, "SongName")},
          {"SingerName", ReadString(song, "SingerName")},
          {"FileHash", ReadString(song, "FileHash")},
          {"FileName", ReadString(song, "FileName")},
      });
    }
  }

  if (records.empty()) return {{"status", 1}, {"data", nlohmann::json::array()}};
  return {
      {"status", 1},
      {"data", {{{"LableName", "单曲"}, {"RecordDatas", records}}}},
  };
}

}  // namespace echo::core

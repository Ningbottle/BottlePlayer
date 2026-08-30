#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace echo::core {

struct DeviceInfo {
  std::string dfid;
  std::string mid;
  std::string uuid;
  std::string guid;
  std::string serverDev;
  std::string mac;
  std::string appid;
  std::string clientver;
  // True once /risk/v2/r_register_dev returned a real, KuGou-issued dfid.
  // Random local dfids get rejected by /v5/url with errcode 20018/20028,
  // /user/playlist with 20017, and /user/vip/detail upstream fails. Once
  // a registered dfid is set here, those endpoints accept the device.
  bool registered = false;
};

struct SessionInfo {
  std::string token;
  std::string userId;
  std::string t1;
  std::string nickname;
  std::string pic;
  // 概念版 VIP 令牌：登录/刷新响应里的 vip_token。
  // tracker v6/priv_url 的 tracker_param.viptoken 需要它来判断 VIP 试听 vs 完整流。
  std::string vipToken;
};

struct AppSettings {
  double volume = 0.48;
  std::string startupPage = "home";
  std::size_t imageMemoryCacheMb = 32;
};

struct SongArtist {
  std::string id;
  std::string name;
};

struct Song {
  std::string id;
  std::string title;
  std::string artist;
  std::string album;
  std::string albumId;
  std::int64_t durationSeconds = 0;
  std::string coverUrl;
  std::string audioUrl;
  std::string hash;
  std::string mvHash;
  std::string mixSongId;
  std::string fileId;
  std::vector<SongArtist> artists;
};

struct Playlist {
  std::string id;
  std::string name;
  std::string coverUrl;
  std::int64_t songCount = 0;
  std::vector<Song> songs;
};

struct User {
  std::string userId;
  std::string nickname;
  std::string avatarUrl;
  std::string token;
};

struct Album {
  std::string id;
  std::string name;
  std::string artist;
  std::string coverUrl;
};

struct Artist {
  std::string id;
  std::string name;
  std::string avatarUrl;
};

struct CommentPage {
  std::int64_t total = 0;
};

struct VideoInfo {
  std::string id;
  std::string title;
  std::string url;
};

struct LyricLine {
  std::int64_t timeMs = 0;
  std::string text;
};

struct LyricDocument {
  std::string raw;
  std::vector<LyricLine> lines;
};

enum class PlaybackStateKind {
  Idle,
  Opening,
  Playing,
  Paused,
  Buffering,
  Stopped,
  Failed,
};

struct PlaybackState {
  PlaybackStateKind kind = PlaybackStateKind::Idle;
  std::string sourceUrl;
  double durationSeconds = 0.0;
  double currentSeconds = 0.0;
  double volume = 1.0;
  double rate = 1.0;
  std::string error;
};

}  // namespace echo::core

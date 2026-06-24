#pragma once

#include <string>

#include "echo/storage/Database.h"

namespace echo::stats {

struct PlayRecord {
  std::string songHash;
  std::string songName;
  std::string singerName;
  std::string albumId;
  std::string albumName;
  std::string coverUrl;
  double durationSeconds = 0;
  bool completed = false;
  double listenedSeconds = 0;
  std::string quality;
  long long playedAtMs = 0;
};

class PlayStatsService {
 public:
  explicit PlayStatsService(echo::storage::Database& db);

  bool RecordPlay(const PlayRecord& record);
  std::string GetSummary(const std::string& range);
  std::string GetTop(const std::string& dim, const std::string& range, int limit);
  std::string GetTimeline(const std::string& range);
  std::string GetRecent(int limit, int offset);
  std::string GetRecommendations(int limit);

 private:
  echo::storage::Database& db_;
  long long RangeToTimestamp(const std::string& range);
};

}  // namespace echo::stats

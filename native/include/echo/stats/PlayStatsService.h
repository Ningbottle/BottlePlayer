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

// B06/B10: RecordPlay's outcome is diagnosable, never a silent no-op.
//   Recorded        - row inserted.
//   BelowThreshold  - listen time at/below the counting threshold; a normal
//                     outcome (short listens are not counted), not an error.
//   InvalidRecord   - rejected input: empty song identity, non-finite or
//                     negative duration/listen time, negative played_at.
//                     playedAtMs == 0 degrades to "now" (the event is being
//                     recorded when the play happened) instead of poisoning
//                     the timeline with epoch-zero rows.
//   StorageError    - the database insert failed (B03 error propagation).
enum class RecordStatus {
  Recorded,
  BelowThreshold,
  InvalidRecord,
  StorageError,
};

class PlayStatsService {
 public:
  explicit PlayStatsService(echo::storage::Database& db);

  RecordStatus RecordPlay(const PlayRecord& record);
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

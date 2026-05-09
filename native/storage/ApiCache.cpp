#include "echo/storage/ApiCache.h"

namespace echo::storage {
namespace {

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

ApiCache::ApiCache(Database& database) : database_(database) {}

std::optional<nlohmann::json> ApiCache::Get(const std::string& cacheKey) {
  return database_.GetApiCache(cacheKey, NowSeconds());
}

void ApiCache::Put(
    const std::string& cacheKey,
    const nlohmann::json& payload,
    std::chrono::seconds ttl) {
  database_.PutApiCache(cacheKey, payload, NowSeconds() + ttl.count());
}

void ApiCache::PruneExpired() {
  database_.PruneExpiredApiCache(NowSeconds());
}

}  // namespace echo::storage


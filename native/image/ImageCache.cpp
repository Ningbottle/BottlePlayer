#include "echo/image/ImageCache.h"

#include <utility>

namespace echo::image {

MemoryImageCache::MemoryImageCache(std::size_t byteBudget)
    : byteBudget_(byteBudget) {}

void MemoryImageCache::Put(std::string key, std::vector<std::uint8_t> bytes) {
  Put(std::move(key), 0, 0, std::move(bytes));  // delegates to 4-arg Put which locks
}

void MemoryImageCache::Put(std::string key,
                           std::uint32_t width,
                           std::uint32_t height,
                           std::vector<std::uint8_t> bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = index_.find(key);
  if (existing != index_.end()) {
    byteCount_ -= existing->second->bytes.size();
    lru_.erase(existing->second);
    index_.erase(existing);
  }

  byteCount_ += bytes.size();
  lru_.push_front(Entry{std::move(key), width, height, std::move(bytes)});
  index_[lru_.front().key] = lru_.begin();
  TrimToBudget();
}

std::optional<ImageBytes> MemoryImageCache::Get(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = index_.find(key);
  if (found == index_.end()) {
    return std::nullopt;
  }

  lru_.splice(lru_.begin(), lru_, found->second);
  return ImageBytes{lru_.front().key, lru_.front().width, lru_.front().height, lru_.front().bytes};
}

void MemoryImageCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  lru_.clear();
  index_.clear();
  byteCount_ = 0;
}

ImageCacheStats MemoryImageCache::Stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ImageCacheStats{index_.size(), byteCount_, byteBudget_};
}

void MemoryImageCache::TrimToBudget() {
  while (byteCount_ > byteBudget_ && !lru_.empty()) {
    auto& entry = lru_.back();
    byteCount_ -= entry.bytes.size();
    index_.erase(entry.key);
    lru_.pop_back();
  }
}

}  // namespace echo::image

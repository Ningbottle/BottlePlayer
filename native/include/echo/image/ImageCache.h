#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace echo::image {

struct ImageBytes {
  std::string key;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> bytes;
};

struct ImageCacheStats {
  std::size_t itemCount = 0;
  std::size_t byteCount = 0;
  std::size_t byteBudget = 0;
};

class MemoryImageCache {
 public:
  explicit MemoryImageCache(std::size_t byteBudget = 16 * 1024 * 1024);

  void Put(std::string key, std::vector<std::uint8_t> bytes);
  void Put(std::string key,
           std::uint32_t width,
           std::uint32_t height,
           std::vector<std::uint8_t> bytes);
  std::optional<ImageBytes> Get(const std::string& key);
  void Clear();

  ImageCacheStats Stats() const;

 private:
  struct Entry {
    std::string key;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bytes;
  };

  void TrimToBudget();

  std::size_t byteBudget_;
  std::size_t byteCount_ = 0;
  std::list<Entry> lru_;
  std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

}  // namespace echo::image

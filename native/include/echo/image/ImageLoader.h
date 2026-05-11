#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "echo/async/TaskScheduler.h"
#include "echo/image/ImageCache.h"

namespace echo::image {

struct DecodedImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> bgra;
  bool placeholder = false;
  bool cancelled = false;
  bool fromMemoryCache = false;
  std::string error;
};

class WicImageDecoder {
 public:
  DecodedImage DecodeFile(const std::filesystem::path& path) const;
  static DecodedImage Placeholder(std::string error);
};

class DiskImageCache {
 public:
  explicit DiskImageCache(std::filesystem::path root, std::size_t byteBudget = 128 * 1024 * 1024);

  void Put(const std::string& key, const std::vector<std::uint8_t>& bytes);
  std::optional<std::vector<std::uint8_t>> Get(const std::string& key) const;
  ImageCacheStats Stats() const;
  std::filesystem::path PathForKey(const std::string& key) const;

 private:
  void TrimToBudget() const;

  std::filesystem::path root_;
  std::size_t byteBudget_;
};

class ImageLoader {
 public:
  ImageLoader(MemoryImageCache& memoryCache, DiskImageCache& diskCache);

  DecodedImage LoadFile(const std::string& key,
                        const std::filesystem::path& path,
                        async::CancellationToken token);
  struct RemoteFetchResult {
    long statusCode = 0;
    std::vector<std::uint8_t> bytes;
    std::string error;
  };

  using RemoteFetch = std::function<RemoteFetchResult(const std::string& url)>;

  DecodedImage LoadRemote(const std::string& key,
                          const std::string& url,
                          RemoteFetch fetch,
                          async::CancellationToken token);

 private:
  MemoryImageCache& memoryCache_;
  DiskImageCache& diskCache_;
  WicImageDecoder decoder_;
};

}  // namespace echo::image

#include "echo/image/ImageLoader.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <objbase.h>
#include <wincodec.h>
#include <windows.h>

namespace echo::image {
namespace {

template <typename T>
void SafeRelease(T*& value) {
  if (value) {
    value->Release();
    value = nullptr;
  }
}

std::string HashKey(const std::string& key) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto ch : key) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= 1099511628211ull;
  }

  std::ostringstream stream;
  stream << std::hex << std::setw(16) << std::setfill('0') << hash;
  return stream.str();
}

std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }

  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  if (size <= 0) {
    return {};
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

bool WriteAllBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

std::uintmax_t FileSizeOrZero(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  return error ? 0 : size;
}

}  // namespace

DecodedImage WicImageDecoder::DecodeFile(const std::filesystem::path& path) const {
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool shouldUninitialize = SUCCEEDED(apartment);
  if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
    return Placeholder("wic_com_init_failed");
  }

  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;

  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (SUCCEEDED(hr)) {
    hr = factory->CreateDecoderFromFilename(
        path.wstring().c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
  }
  if (SUCCEEDED(hr)) {
    hr = decoder->GetFrame(0, &frame);
  }
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }

  DecodedImage image;
  if (SUCCEEDED(hr)) {
    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (SUCCEEDED(hr) && width > 0 && height > 0) {
      const auto stride = width * 4;
      const auto byteCount = stride * height;
      image.width = width;
      image.height = height;
      image.bgra.resize(byteCount);
      hr = converter->CopyPixels(nullptr, stride, byteCount, image.bgra.data());
    }
  }

  SafeRelease(converter);
  SafeRelease(frame);
  SafeRelease(decoder);
  SafeRelease(factory);
  if (shouldUninitialize) {
    CoUninitialize();
  }

  if (FAILED(hr) || image.bgra.empty()) {
    return Placeholder("wic_decode_failed");
  }

  return image;
}

DecodedImage WicImageDecoder::Placeholder(std::string error) {
  DecodedImage image;
  image.width = 1;
  image.height = 1;
  image.bgra = {0xD8, 0xD8, 0xD8, 0xFF};
  image.placeholder = true;
  image.error = std::move(error);
  return image;
}

DiskImageCache::DiskImageCache(std::filesystem::path root, std::size_t byteBudget)
    : root_(std::move(root)), byteBudget_(byteBudget) {
  std::error_code error;
  std::filesystem::create_directories(root_, error);
}

void DiskImageCache::Put(const std::string& key, const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty()) {
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(root_, error);
  if (error) {
    return;
  }

  if (WriteAllBytes(PathForKey(key), bytes)) {
    TrimToBudget();
  }
}

std::optional<std::vector<std::uint8_t>> DiskImageCache::Get(const std::string& key) const {
  auto bytes = ReadAllBytes(PathForKey(key));
  if (bytes.empty()) {
    return std::nullopt;
  }
  return bytes;
}

ImageCacheStats DiskImageCache::Stats() const {
  ImageCacheStats stats;
  stats.byteBudget = byteBudget_;

  std::error_code error;
  if (!std::filesystem::exists(root_, error)) {
    return stats;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    stats.itemCount += 1;
    stats.byteCount += static_cast<std::size_t>(FileSizeOrZero(entry.path()));
  }

  return stats;
}

std::filesystem::path DiskImageCache::PathForKey(const std::string& key) const {
  return root_ / (HashKey(key) + ".img");
}

void DiskImageCache::TrimToBudget() const {
  auto stats = Stats();
  if (stats.byteCount <= byteBudget_) {
    return;
  }

  struct FileEntry {
    std::filesystem::path path;
    std::filesystem::file_time_type lastWrite;
    std::uintmax_t size = 0;
  };

  std::vector<FileEntry> files;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
    if (error || !entry.is_regular_file()) {
      continue;
    }
    files.push_back(FileEntry{
        entry.path(),
        entry.last_write_time(error),
        FileSizeOrZero(entry.path())});
  }

  std::sort(files.begin(), files.end(), [](const FileEntry& left, const FileEntry& right) {
    return left.lastWrite < right.lastWrite;
  });

  for (const auto& file : files) {
    if (stats.byteCount <= byteBudget_) {
      break;
    }
    std::filesystem::remove(file.path, error);
    if (!error) {
      stats.byteCount -= static_cast<std::size_t>(file.size);
    }
  }
}

ImageLoader::ImageLoader(MemoryImageCache& memoryCache, DiskImageCache& diskCache)
    : memoryCache_(memoryCache), diskCache_(diskCache) {}

DecodedImage ImageLoader::LoadFile(const std::string& key,
                                   const std::filesystem::path& path,
                                   async::CancellationToken token) {
  if (token.IsCancellationRequested()) {
    auto image = WicImageDecoder::Placeholder("cancelled");
    image.cancelled = true;
    return image;
  }

  if (const auto cached = memoryCache_.Get(key)) {
    DecodedImage image;
    image.width = cached->width;
    image.height = cached->height;
    image.bgra = cached->bytes;
    image.fromMemoryCache = true;
    return image;
  }

  auto decoded = decoder_.DecodeFile(path);
  if (token.IsCancellationRequested()) {
    auto image = WicImageDecoder::Placeholder("cancelled");
    image.cancelled = true;
    return image;
  }

  if (decoded.placeholder) {
    return decoded;
  }

  memoryCache_.Put(key, decoded.width, decoded.height, decoded.bgra);
  diskCache_.Put(key, ReadAllBytes(path));
  return decoded;
}

}  // namespace echo::image

#include "echo/core/DeviceService.h"

#include <array>
#include <random>
#include <sstream>

namespace echo::core {
namespace {

std::string RandomHex(std::size_t length) {
  static constexpr std::array<char, 16> kHex = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::random_device random;
  std::mt19937 generator(random());
  std::uniform_int_distribution<int> dist(0, 15);

  std::string value;
  value.reserve(length);
  for (std::size_t index = 0; index < length; ++index) {
    value.push_back(kHex[static_cast<std::size_t>(dist(generator))]);
  }
  return value;
}

std::string RandomGuidLike() {
  std::ostringstream stream;
  stream << RandomHex(8) << '-' << RandomHex(4) << '-' << RandomHex(4) << '-' << RandomHex(4)
         << '-' << RandomHex(12);
  return stream.str();
}

DeviceInfo CreateDeviceInfo() {
  const auto guid = RandomGuidLike();
  return DeviceInfo{
      .dfid = RandomHex(32),
      .mid = RandomHex(32),
      .uuid = guid,
      .guid = guid,
      .serverDev = "",
      .mac = RandomHex(12),
      .appid = "1014",
      .clientver = "20000",
  };
}

}  // namespace

DeviceService::DeviceService(storage::DeviceRepository& devices) : devices_(devices) {}

DeviceInfo DeviceService::EnsureDeviceReady() {
  if (auto existing = devices_.Load(); existing && !existing->dfid.empty()) {
    return *existing;
  }

  auto device = CreateDeviceInfo();
  devices_.Save(device);
  return device;
}

}  // namespace echo::core


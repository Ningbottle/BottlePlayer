#include "echo/core/DeviceService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"

#include <array>
#include <iostream>
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

void NormalizeDeviceInfo(DeviceInfo& device) {
  const bool placeholderDfid = device.dfid.empty() || device.dfid == "-";
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);

  if (device.appid != profile.appid) {
    device.registered = false;
  }
  device.appid = profile.appid;
  device.clientver = profile.clientver;

  if (placeholderDfid) {
    device.registered = false;
    return;
  }

  if (device.mid.empty()) {
    const std::string md5Dfid = CalculateMd5(device.dfid);
    device.mid = md5Dfid + md5Dfid.substr(0, 7);
  }
  if (device.uuid.empty()) {
    device.uuid = CalculateMd5(device.dfid + device.mid);
  }
}

DeviceInfo CreateDeviceInfo() {
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  const auto guid = RandomGuidLike();
  DeviceInfo device{
      .dfid = "-",
      .mid = "",
      .uuid = "",
      .guid = guid,
      .serverDev = "",
      .mac = "02:00:00:00:00:00",
      .appid = profile.appid,
      .clientver = profile.clientver,
      .registered = false,
  };
  NormalizeDeviceInfo(device);
  return device;
}

}  // namespace

DeviceService::DeviceService(storage::DeviceRepository& devices) : devices_(devices) {}

DeviceInfo DeviceService::EnsureDeviceReady() {
  DeviceInfo device;
  if (auto existing = devices_.Load(); existing) {
    device = *existing;
  } else {
    device = CreateDeviceInfo();
    devices_.Save(device);
  }

  // Normalize in-memory before returning to business code.
  // Old records with random mid/uuid are overwritten here; the db is NOT modified.
  NormalizeDeviceInfo(device);

  return device;
}

}  // namespace echo::core


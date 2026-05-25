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
  // Device default appid is 1014 (KuGou's "web/lite" identifier) — this is
  // what QR login (/v2/qrcode) and most session-management endpoints expect.
  // Endpoints that need a different appid (e.g. /song/url, /youth/day/vip*)
  // hardcode their own appid; the shared mid/dfid/uuid carry across all.
  // Explicit `registered=false` because MSVC's designated initializer does
  // not respect NSDMI defaults for omitted fields (UB without this line).
  return DeviceInfo{
      .dfid = RandomHex(32),
      .mid = RandomHex(32),
      .uuid = guid,
      .guid = guid,
      .serverDev = "",
      .mac = RandomHex(12),
      .appid = "1014",
      .clientver = "20000",
      .registered = false,
  };
}

}  // namespace

DeviceService::DeviceService(storage::DeviceRepository& devices) : devices_(devices) {}

DeviceInfo DeviceService::EnsureDeviceReady() {
  if (auto existing = devices_.Load(); existing && !existing->dfid.empty()) {
    // Migrate devices that were briefly created as appid=1005 — KuGou flags
    // those as untrusted via QR login and downgrades VIP audio. Reset to
    // 1014 (the QR-login-friendly identifier) so the next scan can bind
    // a token correctly.
    if (existing->appid == "1014") {
      return *existing;
    }
    // Fall through to regenerate with the canonical 1014/20000 fingerprint.
  }

  auto device = CreateDeviceInfo();
  devices_.Save(device);
  return device;
}

}  // namespace echo::core


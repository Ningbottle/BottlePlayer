#include "echo/core/DeviceService.h"
#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"
#include "echo/diagnostics/EchoDiagnostics.h"

#include <algorithm>
#include <array>
#include <cctype>
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
    ECHO_LOG("DeviceSvc", "normalize appid_mismatch stored=" + device.appid +
        " expected=" + profile.appid + " -> registered=N");
  }
  device.appid = profile.appid;
  device.clientver = profile.clientver;

  if (placeholderDfid) {
    device.registered = false;
    return;
  }

  device = BackfillInMemoryIdentity(std::move(device));
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

DeviceInfo BackfillInMemoryIdentity(DeviceInfo device) {
  const bool placeholderDfid = device.dfid.empty() || device.dfid == "-";
  if (placeholderDfid) return device;
  if (device.mid.empty()) {
    const std::string md5Dfid = CalculateMd5(device.dfid);
    device.mid = md5Dfid + md5Dfid.substr(0, 7);
  }
  if (device.uuid.empty()) {
    device.uuid = CalculateMd5(device.dfid + device.mid);
  }
  return device;
}

DeviceService::DeviceService(storage::DeviceRepository& devices) : devices_(devices) {}

DeviceInfo DeviceService::EnsureDeviceReady() {
  DeviceInfo device;
  bool recordUsable = false;
  if (auto existing = devices_.Load(); existing) {
    // DeviceRepository::Clear() persists `{}` and Load() hands that tombstone
    // back as if a device existed. A record without any identity anchor
    // (guid AND uuid empty AND no real dfid) cannot carry the registration
    // bloodline: the QR chain would run on an identity with no
    // guid/dfid/mid/uuid (observed 2026-09-14 22:45). Treat it as missing.
    // A legacy dfid-bearing record is NOT covered by this rule — the
    // uuid/guid backfill migration below must keep its dfid bloodline.
    const bool dfidPlaceholder = existing->dfid.empty() || existing->dfid == "-";
    const bool noIdentityAnchor =
        existing->guid.empty() && existing->uuid.empty() && dfidPlaceholder;
    if (!noIdentityAnchor) {
      device = *existing;
      recordUsable = true;
    } else {
      ECHO_LOG("DeviceSvc",
               "empty device record treated as missing -> recreating identity");
    }
  }
  if (!recordUsable) {
    device = CreateDeviceInfo();
    devices_.Save(device);
    ECHO_LOG("DeviceSvc", "created fresh device identity (registered=N dfid=-)");
  }

  // Normalize in-memory before returning to business code.
  // Old records with random mid/uuid are overwritten here.
  NormalizeDeviceInfo(device);

  // Records created before guid persistence used uuid as the encrypted
  // registration fingerprint, while ResolveAndroidMid fell back to an
  // unrelated legacy mid. Backfill the same stable uuid as guid so both the
  // fingerprint and Android mid share one identity bloodline. Persist the
  // migration and require a fresh KuGou registration before protected APIs
  // trust the old dfid again.
  if (device.guid.empty() && !device.uuid.empty()) {
    device.guid = device.uuid;
    device.registered = false;
    devices_.Save(device);
    ECHO_LOG("DeviceSvc", "backfilled guid from uuid -> registered=N (re-registration required)");
  }

  return device;
}

std::string ResolveAndroidMid(const DeviceInfo& device) {
  const bool storedMidLooksAndroid =
      device.mid.size() >= 38 &&
      device.mid.size() <= 39 &&
      std::all_of(device.mid.begin(), device.mid.end(),
                  [](unsigned char c) { return std::isdigit(c); });
  if (storedMidLooksAndroid) return device.mid;
  if (!device.guid.empty()) return CalculateAndroidMid(device.guid);
  if (!device.mid.empty()) return CalculateAndroidMid(device.mid);
  return "0";
}

}  // namespace echo::core


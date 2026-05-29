#include "echo/core/CompatApiUtils.h"
#include "echo/core/CompatRequestContext.h"
#include "echo/core/JsonHelpers.h"
#include "echo/core/DeviceRegisterService.h"
#include "echo/core/DeviceService.h"
#include "echo/core/LoginService.h"
#include "echo/diagnostics/Redaction.h"
#include "echo/storage/DeviceRepository.h"
#include "echo/storage/SessionRepository.h"

#include <sstream>

namespace echo::core {

CompatResponse HandleLoginQrKey(
    storage::Database& database,
    const std::function<nlohmann::json(const DeviceInfo&)>& handler) {
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  const auto device = devices.EnsureDeviceReady();
  if (handler) {
    return JsonResponse(handler(device));
  }
  LoginService login;
  return JsonResponse(login.BeginQrLogin(device));
}

CompatResponse HandleLoginQrCreate(const QueryMap& query) {
  const auto key = QueryValue(query, "key");
  if (key.empty()) {
    return JsonResponse({{"status", 0}, {"error", "missing key parameter"}, {"data", nullptr}});
  }
  const auto qrcodeUrl = "https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode=" + key;
  return JsonResponse({
      {"status", 1},
      {"data", {{"qrcode", key}, {"qrcodeurl", qrcodeUrl}}},
  });
}

CompatResponse HandleLoginQrCheck(
    storage::Database& database,
    const QueryMap& query,
    const std::function<nlohmann::json(const DeviceInfo&, std::string)>& handler) {
  const auto key = QueryValue(query, "key");
  if (key.empty()) {
    return JsonResponse({{"status", 0}, {"error", "missing key parameter"}, {"data", nullptr}});
  }
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  const auto device = devices.EnsureDeviceReady();
  nlohmann::json result;
  if (handler) {
    result = handler(device, key);
  } else {
    LoginService login;
    result = login.PollQrLogin(device, key);
  }

  auto ExtractUserId = [](const nlohmann::json& j, const std::string& key) -> std::string {
    if (!j.contains(key)) return "";
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<std::int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<std::uint64_t>());
    return "";
  };

  const nlohmann::json* loginData = nullptr;
  if (result.contains("data") && result["data"].is_object() &&
      result["data"].value("status", 0) == 4) {
    loginData = &result["data"];
  } else if (result.value("status", 0) == 4) {
    loginData = &result;
  }

  if (loginData) {
    auto FirstNonEmptyString = [&](std::initializer_list<const char*> keys) {
      for (const char* k : keys) {
        if (loginData->contains(k) && (*loginData)[k].is_string()) {
          auto v = (*loginData)[k].get<std::string>();
          if (!v.empty()) return v;
        }
      }
      return std::string{};
    };
    SessionInfo session;
    session.token = loginData->value("token", "");
    session.userId = ExtractUserId(*loginData, "userid");
    session.nickname = FirstNonEmptyString({"nickname", "username", "name"});
    session.pic = FirstNonEmptyString({"pic", "headphoto", "avatar", "headerurl", "userpic"});
    if (!session.token.empty() && !session.userId.empty()) {
      storage::SessionRepository sessionRepo(database);
      sessionRepo.Save(session);

      if (!device.registered) {
        DeviceRegisterService registerSvc;
        std::string regError;
        const auto newDfid = registerSvc.Register(device, session.userId, session.token, &regError);
        if (!newDfid.empty()) {
          DeviceInfo updated = device;
          updated.dfid = newDfid;
          updated.registered = true;
          storage::DeviceRepository devRepo(database);
          devRepo.Save(updated);
        } else {
          ECHO_LOG("CompatApi", std::string("Device registration failed: ") + regError);
        }
      }
    }
  }
  return JsonResponse(result);
}

CompatResponse HandleAuthLogout(storage::Database& database) {
  storage::SessionRepository sessionRepo(database);
  sessionRepo.Clear();
  storage::DeviceRepository deviceRepo(database);
  deviceRepo.Clear();
  return JsonResponse({{"status", 1}, {"data", {{"cleared", true}}}});
}

CompatResponse HandleSettingsDevice(
    storage::Database& database,
    const QueryMap& query) {
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  auto device = devices.EnsureDeviceReady();

  const auto newDfid = QueryValue(query, "dfid");
  const auto newMid = QueryValue(query, "mid");
  const auto newUuid = QueryValue(query, "uuid");
  const bool clearOverride = QueryValue(query, "clear") == "1";

  bool changed = false;
  if (clearOverride) {
    deviceRepo.Clear();
    device = devices.EnsureDeviceReady();
    changed = true;
  }
  if (!newDfid.empty() && newDfid != device.dfid) {
    device.dfid = newDfid;
    changed = true;
  }
  if (!newMid.empty() && newMid != device.mid) {
    device.mid = newMid;
    changed = true;
  }
  if (!newUuid.empty() && newUuid != device.uuid) {
    device.uuid = newUuid;
    changed = true;
  }
  if (changed) {
    if (!newDfid.empty() || !newMid.empty() || !newUuid.empty()) {
      device.registered = true;
    }
    deviceRepo.Save(device);
    {
      std::ostringstream log;
      log << "/settings/device updated dfid=" << echo::diagnostics::MaskMiddle(device.dfid)
          << " mid=" << echo::diagnostics::MaskMiddle(device.mid)
          << " uuid=" << echo::diagnostics::MaskMiddle(device.uuid);
      ECHO_LOG("CompatApi", log.str());
    }
  }
  return JsonResponse({
      {"status", 1},
      {"data", ToJson(device)},
      {"updated", changed},
  });
}

}  // namespace echo::core

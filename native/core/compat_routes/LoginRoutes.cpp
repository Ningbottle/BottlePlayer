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
    const std::function<nlohmann::json(const DeviceInfo&, std::string)>& handler,
    const LoginHttpGet& sessionHttpGet,
    const LoginHttpPost& sessionHttpPost,
    const std::function<std::string(const DeviceInfo&, const std::string&,
                                    const std::string&, std::string*)>&
        qrRegisterHandler) {
  const auto key = QueryValue(query, "key");
  if (key.empty()) {
    return JsonResponse({{"status", 0}, {"error", "missing key parameter"}, {"data", nullptr}});
  }
  storage::DeviceRepository deviceRepo(database);
  DeviceService devices(deviceRepo);
  // Mutable: the register step below advances THIS object in place so the
  // credential refresh and the persisted record travel on the final identity.
  auto device = devices.EnsureDeviceReady();
  nlohmann::json result;
  if (handler) {
    result = handler(device, key);
  } else {
    // The poll is a GET upstream. Once any verb is injected the poller runs on
    // the explicit pair, so a verb the host did not wire fails loudly instead of
    // silently falling back to the real network. With nothing injected the
    // default transport is used unchanged, which is the production path.
    LoginService login = (sessionHttpGet || sessionHttpPost)
        ? LoginService(sessionHttpGet, sessionHttpPost)
        : LoginService();
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
    auto FirstInt = [&](std::initializer_list<const char*> keys) {
      for (const char* k : keys) {
        if (!loginData->contains(k)) continue;
        const auto& value = (*loginData)[k];
        if (value.is_number_integer()) return value.get<int>();
        if (value.is_number_unsigned()) return static_cast<int>(value.get<unsigned int>());
        if (value.is_string()) {
          try {
            return std::stoi(value.get<std::string>());
          } catch (...) {
          }
        }
      }
      return 0;
    };
    SessionInfo session;
    session.token = loginData->value("token", "");
    session.userId = ExtractUserId(*loginData, "userid");
    session.nickname = FirstNonEmptyString({"nickname", "username", "name"});
    session.pic = FirstNonEmptyString({"pic", "headphoto", "avatar", "headerurl", "userpic"});
    // 概念版登录响应可能直接下发 vip_token / t1；留空则后续由刷新链路补齐。
    session.vipToken = FirstNonEmptyString({"vip_token", "viptoken"});
    session.vipType = FirstInt({"vip_type", "vipType"});
    session.t1 = FirstNonEmptyString({"t1"});
    if (!session.token.empty() && !session.userId.empty()) {
      // Order matters (20017 investigation): an unregistered device must
      // register BEFORE the credential refresh. LoginService::RefreshSession
      // reaches the Concept family only when the device it receives is
      // registered with a real dfid; the pre-fix ordering refreshed first on
      // the unregistered device and registered into a throwaway copy last,
      // so the Concept family never fired (2026-09-14 22:45 log).
      // The dfid-change flag is computed BEFORE the overwrite: an assign-
      // then-compare would always report N.
      if (!device.registered) {
        std::string regError;
        // Injected seam (offline contract tests) falls back to the real
        // service; same signature as DeviceRegisterService::Register.
        const auto newDfid =
            qrRegisterHandler
                ? qrRegisterHandler(device, session.userId, session.token, &regError)
                : DeviceRegisterService().Register(device, session.userId, session.token, &regError);
        if (!newDfid.empty()) {
          const bool dfidChanged = newDfid != device.dfid;
          device.dfid = newDfid;
          device.registered = true;
          storage::DeviceRepository devRepo(database);
          devRepo.Save(device);
          ECHO_LOG("DeviceRegister", std::string("qr_login_register=success dfid_changed=") +
              (dfidChanged ? "Y " : "N ") + DescribeDeviceIdentity(device));
        } else {
          // Registration failure keeps the device unregistered: the refresh
          // below then stays Standard-only (same as before this fix). Never
          // fake readiness or synthesize a dfid.
          ECHO_LOG("CompatApi", std::string("Device registration failed: ") + regError);
        }
      }

      if (session.vipToken.empty()) {
        // 扫码响应不下发 vip_token；按参考仓 login_token.js 刷新补齐，
        // v6/priv_url 需要它才给会员音质。
        // Same rule as the poller: with any verb injected the refresh runs on the
        // explicit pair, so an unwired POST cannot silently reach the network.
        LoginService loginSvc = (sessionHttpGet || sessionHttpPost)
            ? LoginService(sessionHttpGet, sessionHttpPost)
            : LoginService();
        const auto refreshed = loginSvc.RefreshSession(
            device, session.userId, session.token, session.t1);
        if (refreshed) {
          session.token = refreshed->token;
          session.vipToken = refreshed->vipToken;
          session.vipType = refreshed->vipType;
          if (!refreshed->t1.empty()) session.t1 = refreshed->t1;
        }
        if (!session.vipToken.empty()) {
          ECHO_LOG("VipToken", "refreshed vip_token via login_by_token (len=" + std::to_string(session.vipToken.size()) + ")");
        } else {
          ECHO_LOG("VipToken", "login_by_token refresh returned no vip_token");
        }
      } else {
        ECHO_LOG("CompatApi", "QR login issued vip_token (len=" + std::to_string(session.vipToken.size()) + ")");
      }

      storage::SessionRepository sessionRepo(database);
      sessionRepo.Save(session);
      // Diagnostic (20017 investigation): log the FINAL device identity the
      // stored credentials travel with. Sanitized: lengths and fingerprints
      // only.
      ECHO_LOG("AuthSession", "qr_session_saved token_len=" +
          std::to_string(session.token.size()) +
          " userid_present=" + (session.userId.empty() ? "N" : "Y") +
          " vip_token_len=" + std::to_string(session.vipToken.size()) +
          " t1_len=" + std::to_string(session.t1.size()) +
          " device " + DescribeDeviceIdentity(device));
    } else {
      ECHO_LOG("CompatApi", "QR login issued no vip_token");
    }
  }
  // The native session owns all credentials. Keep QR results safe even when
  // this route is exercised independently of the CompatApi chokepoint.
  StripSessionCredentials(result);
  return JsonResponse(result);
}

CompatResponse HandleAuthLogout(storage::Database& database) {
  storage::SessionRepository sessionRepo(database);
  storage::DeviceRepository deviceRepo(database);
  // Diagnostic (20017 investigation): record what a logout destroys. The
  // device record carries the dfid/mid/guid bloodline the token was issued
  // against; wiping it forces the next QR login onto a brand-new identity.
  if (const auto device = deviceRepo.Load()) {
    ECHO_LOG("AuthLogout", std::string("clearing session+device (registered=") +
        (device->registered ? "Y" : "N") +
        " dfid_len=" + std::to_string(device->dfid == "-" ? 0 : device->dfid.size()) +
        " guid_present=" + (device->guid.empty() ? "N" : "Y") +
        ") next_qr_login_will_run_on_fresh_identity");
  }
  sessionRepo.Clear();
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

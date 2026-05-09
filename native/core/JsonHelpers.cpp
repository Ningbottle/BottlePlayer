#include "echo/core/JsonHelpers.h"

namespace echo::core {

nlohmann::json ToJson(const DeviceInfo& device) {
  return {
      {"dfid", device.dfid},
      {"mid", device.mid},
      {"uuid", device.uuid},
      {"guid", device.guid},
      {"serverDev", device.serverDev},
      {"mac", device.mac},
      {"appid", device.appid},
      {"clientver", device.clientver},
  };
}

nlohmann::json ToJson(const SessionInfo& session) {
  return {
      {"token", session.token},
      {"userid", session.userId},
      {"t1", session.t1},
  };
}

DeviceInfo DeviceInfoFromJson(const nlohmann::json& value) {
  DeviceInfo device;
  device.dfid = value.value("dfid", "");
  device.mid = value.value("mid", "");
  device.uuid = value.value("uuid", "");
  device.guid = value.value("guid", "");
  device.serverDev = value.value("serverDev", "");
  device.mac = value.value("mac", "");
  device.appid = value.value("appid", "");
  device.clientver = value.value("clientver", "");
  return device;
}

SessionInfo SessionInfoFromJson(const nlohmann::json& value) {
  SessionInfo session;
  session.token = value.value("token", "");
  session.userId = value.value("userid", "");
  session.t1 = value.value("t1", "");
  return session;
}

}  // namespace echo::core


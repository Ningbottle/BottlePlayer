#include "echo/storage/IdentityCodec.h"
#include <limits>
#include <stdexcept>

namespace echo::storage {
namespace {
std::string StringField(const nlohmann::json& value, const char* key) {
  if (!value.is_object()) return {};
  const auto field = value.find(key);
  if (field == value.end() || !field->is_string()) return {};
  return field->get<std::string>();
}

bool BoolField(const nlohmann::json& value, const char* key) {
  if (!value.is_object()) return false;
  const auto field = value.find(key);
  return field != value.end() && field->is_boolean() && field->get<bool>();
}

int IntField(const nlohmann::json& value, const char* key) {
  if (!value.is_object()) return 0;
  const auto field = value.find(key);
  if (field == value.end()) return 0;
  if (field->is_number_unsigned()) {
    const auto number = field->get<std::uint64_t>();
    return number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())
               ? static_cast<int>(number)
               : 0;
  }
  if (field->is_number_integer()) {
    const auto number = field->get<std::int64_t>();
    return number >= std::numeric_limits<int>::min() &&
                   number <= std::numeric_limits<int>::max()
               ? static_cast<int>(number)
               : 0;
  }
  return 0;
}

}
nlohmann::json EncodeDevice(const model::DeviceInfo& device) {
  // Use explicit assignment for the bool field — nlohmann::json's initializer
  // list constructor sometimes misinterprets `{"key", boolValue}` pairs.
  nlohmann::json j = {
      {"dfid", device.dfid},
      {"mid", device.mid},
      {"uuid", device.uuid},
      {"guid", device.guid},
      {"serverDev", device.serverDev},
      {"mac", device.mac},
      {"appid", device.appid},
      {"clientver", device.clientver},
  };
  j["registered"] = device.registered;
  return j;
}

nlohmann::json EncodeSession(const model::SessionInfo& session) {
  return {
      {"token", session.token},
      {"userid", session.userId},
      {"t1", session.t1},
      {"nickname", session.nickname},
      {"pic", session.pic},
      {"vip_token", session.vipToken},
      {"vip_type", session.vipType},
  };
}

model::DeviceInfo DecodeDevice(const nlohmann::json& value) {
  model::DeviceInfo device;
  device.dfid = StringField(value, "dfid");
  device.mid = StringField(value, "mid");
  device.uuid = StringField(value, "uuid");
  device.guid = StringField(value, "guid");
  device.serverDev = StringField(value, "serverDev");
  device.mac = StringField(value, "mac");
  device.appid = StringField(value, "appid");
  device.clientver = StringField(value, "clientver");
  device.registered = BoolField(value, "registered");
  return device;
}

model::SessionInfo DecodeSession(const nlohmann::json& value) {
  model::SessionInfo session;
  session.token = StringField(value, "token");
  session.userId = StringField(value, "userid");
  session.t1 = StringField(value, "t1");
  session.nickname = StringField(value, "nickname");
  session.pic = StringField(value, "pic");
  session.vipToken = StringField(value, "vip_token");
  session.vipType = IntField(value, "vip_type");
  return session;
}


bool IsWellFormedDeviceRecord(const nlohmann::json& payload) {
  if (!payload.is_object()) return false;
  for (const char* key : {"dfid", "mid", "uuid", "guid", "serverDev", "mac", "appid", "clientver"}) {
    const auto field = payload.find(key);
    if (field != payload.end() && !field->is_string()) return false;
  }
  const auto registered = payload.find("registered");
  return registered == payload.end() || registered->is_boolean();
}


std::uint64_t DeviceRevision(const std::optional<nlohmann::json>& value) {
  if (!value || !value->is_object() || !value->contains("_revision")) return 0;
  const auto& rev = (*value)["_revision"];
  if (rev.is_number_unsigned()) return rev.get<std::uint64_t>();
  if (rev.is_number_integer() && rev.get<std::int64_t>() >= 0) return rev.get<std::uint64_t>();
  throw std::runtime_error("invalid_device_revision");
}
nlohmann::json StampDevice(nlohmann::json next, const std::optional<nlohmann::json>& previous) {
  const auto rev = DeviceRevision(previous);
  if (rev == std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("device_revision_exhausted");
  next["_revision"] = rev + 1;
  return next;
}
}  // namespace echo::storage

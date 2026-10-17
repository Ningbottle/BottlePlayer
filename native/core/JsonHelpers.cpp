#include "echo/core/JsonHelpers.h"

#include <cstdint>
#include <limits>
#include <unordered_set>

namespace echo::core {

namespace {

std::string ChildPath(const std::string& parent, const std::string& child) {
  if (parent.empty()) {
    return "/" + child;
  }
  return parent + "/" + child;
}

bool IsIgnored(const std::unordered_set<std::string>& ignoredPaths, const std::string& path) {
  return ignoredPaths.find(path.empty() ? "/" : path) != ignoredPaths.end();
}

bool MatchContractJson(
    const nlohmann::json& expected,
    const nlohmann::json& actual,
    const std::unordered_set<std::string>& ignoredPaths,
    const std::string& path,
    std::vector<std::string>* mismatches) {
  if (IsIgnored(ignoredPaths, path)) {
    return true;
  }

  // Numeric types (integer, unsigned, float) are compared by value so that
  // a positive integer parsed from a fixture file (number_unsigned) matches
  // the same value produced by C++ assignment (number_integer).
  const bool expectedNumeric = expected.is_number();
  const bool actualNumeric   = actual.is_number();
  if (expectedNumeric || actualNumeric) {
    if (!expectedNumeric || !actualNumeric) {
      if (mismatches) mismatches->push_back(path.empty() ? "/" : path);
      return false;
    }
    if (expected.get<double>() != actual.get<double>()) {
      if (mismatches) mismatches->push_back(path.empty() ? "/" : path);
      return false;
    }
    return true;
  }

  if (expected.type() != actual.type()) {
    if (mismatches) {
      mismatches->push_back(path.empty() ? "/" : path);
    }
    return false;
  }

  if (expected.is_object()) {
    bool matches = true;
    for (const auto& item : expected.items()) {
      const auto childPath = ChildPath(path, item.key());
      if (!actual.contains(item.key())) {
        if (mismatches) {
          mismatches->push_back(childPath);
        }
        matches = false;
        continue;
      }

      matches = MatchContractJson(
                    item.value(),
                    actual.at(item.key()),
                    ignoredPaths,
                    childPath,
                    mismatches) &&
                matches;
    }
    return matches;
  }

  if (expected.is_array()) {
    if (actual.size() < expected.size()) {
      if (mismatches) {
        mismatches->push_back(path.empty() ? "/" : path);
      }
      return false;
    }

    bool matches = true;
    for (std::size_t index = 0; index < expected.size(); ++index) {
      matches = MatchContractJson(
                    expected.at(index),
                    actual.at(index),
                    ignoredPaths,
                    ChildPath(path, std::to_string(index)),
                    mismatches) &&
                matches;
    }
    return matches;
  }

  if (expected != actual) {
    if (mismatches) {
      mismatches->push_back(path.empty() ? "/" : path);
    }
    return false;
  }

  return true;
}

// Stored records are read with these instead of json::value(): value() throws
// on a non-object or a wrong-typed field, which turned one damaged KV record
// into a 500 on every route that loads it (including the clear/logout paths
// that would repair it). Wrong types read as the field default.
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

}  // namespace

nlohmann::json ToJson(const DeviceInfo& device) {
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

nlohmann::json ToJson(const SessionInfo& session) {
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

DeviceInfo DeviceInfoFromJson(const nlohmann::json& value) {
  DeviceInfo device;
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

SessionInfo SessionInfoFromJson(const nlohmann::json& value) {
  SessionInfo session;
  session.token = StringField(value, "token");
  session.userId = StringField(value, "userid");
  session.t1 = StringField(value, "t1");
  session.nickname = StringField(value, "nickname");
  session.pic = StringField(value, "pic");
  session.vipToken = StringField(value, "vip_token");
  session.vipType = IntField(value, "vip_type");
  return session;
}

bool ContractJsonMatches(
    const nlohmann::json& expected,
    const nlohmann::json& actual,
    const std::vector<std::string>& ignoredPaths,
    std::vector<std::string>* mismatches) {
  if (mismatches) {
    mismatches->clear();
  }

  const std::unordered_set<std::string> ignored(ignoredPaths.begin(), ignoredPaths.end());
  return MatchContractJson(expected, actual, ignored, "", mismatches);
}

}  // namespace echo::core

#include "echo/core/JsonHelpers.h"

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
  device.registered = value.value("registered", false);
  return device;
}

SessionInfo SessionInfoFromJson(const nlohmann::json& value) {
  SessionInfo session;
  session.token = value.value("token", "");
  session.userId = value.value("userid", "");
  session.t1 = value.value("t1", "");
  session.nickname = value.value("nickname", "");
  session.pic = value.value("pic", "");
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

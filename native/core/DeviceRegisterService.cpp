#include "echo/core/DeviceRegisterService.h"

#include "echo/core/Crypto.h"
#include "echo/core/KuGouProfile.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace echo::core {
namespace {

std::string UrlEncode(const std::string& value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      stream << static_cast<char>(ch);
    } else {
      stream << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
  }
  return stream.str();
}

// Build the fixed Xiaomi-Redmi device fingerprint that KuGou expects in the
// AES-encrypted body. Field names + defaults mirror MakcRe's register_dev.js.
nlohmann::json BuildDeviceFingerprint(const DeviceInfo& device) {
  // Use the device's persistent uuid as `imei`/`uuid` so re-registrations
  // are stable across restarts; KuGou ties dfid to these identifiers.
  const std::string identity = !device.guid.empty() ? device.guid : device.uuid;
  return {
      {"availableRamSize", 4983533568LL},
      {"availableRomSize", 48114719LL},
      {"availableSDSize",  48114717LL},
      {"basebandVer",      ""},
      {"batteryLevel",     100},
      {"batteryStatus",    3},
      {"brand",            "Redmi"},
      {"buildSerial",      "unknown"},
      {"device",           "marble"},
      {"imei",             identity},
      {"imsi",             ""},
      {"manufacturer",     "Xiaomi"},
      {"uuid",             identity},
      {"accelerometer",      false},
      {"accelerometerValue", ""},
      {"gravity",            false},
      {"gravityValue",       ""},
      {"gyroscope",          false},
      {"gyroscopeValue",     ""},
      {"light",              false},
      {"lightValue",         ""},
      {"magnetic",           false},
      {"magneticValue",      ""},
      {"orientation",        false},
      {"orientationValue",   ""},
      {"pressure",           false},
      {"pressureValue",      ""},
      {"step_counter",       false},
      {"step_counterValue",  ""},
      {"temperature",        false},
      {"temperatureValue",   ""},
  };
}

std::string AndroidMidForDevice(const DeviceInfo& device) {
  if (!device.guid.empty()) return CalculateAndroidMid(device.guid);
  if (!device.mid.empty()) return CalculateAndroidMid(device.mid);
  return "0";
}

}  // namespace

DeviceRegisterService::DeviceRegisterService()
    : DeviceRegisterService([](
          const std::string& url,
          const std::string& body,
          const std::unordered_map<std::string, std::string>& headers) {
        HttpClient client;
        return client.Post(url, body, headers);
      }) {}

DeviceRegisterService::DeviceRegisterService(DeviceRegisterHttpPost httpPost)
    : httpPost_(std::move(httpPost)) {}

std::string DeviceRegisterService::Register(
    const DeviceInfo& device,
    const std::string& userId,
    const std::string& token,
    std::string* error) const {
  auto setError = [&](const std::string& msg) {
    if (error) *error = msg;
    return std::string{};
  };

  // 1) AES-encrypt the device fingerprint (random 6-char key + MD5-derived
  //    encryptKey/iv, CBC + PKCS7). Returns base64 ciphertext + the seed key.
  const auto fingerprint = BuildDeviceFingerprint(device).dump();
  const auto aes = PlaylistAesEncrypt(fingerprint);
  if (aes.key.empty() || aes.data.empty()) {
    return setError("PlaylistAesEncrypt failed");
  }

  // 2) RSA-PKCS1-v1_5 encrypt the wrapper { aes, uid, token } → hex string.
  //    KuGou's risk service uses this to learn our AES key without ever
  //    seeing it in cleartext.
  //    NOTE: MakcRe uses `forge.util.bytesToHex` which outputs LOWERCASE hex.
  //    Our `RsaPkcs1Encrypt` returns UPPERCASE — must downcase here, otherwise
  //    the signature differs from KuGou's and we get error_code 20010.
  nlohmann::json wrapper = {
      {"aes",   aes.key},
      {"uid",   userId.empty() ? 0 : std::stoll(userId)},
      {"token", token},
  };
  const auto profile = GetKuGouProfile(KuGouEdition::Concept);
  std::string rsaP = RsaPkcs1Encrypt(wrapper.dump(), profile.saltKind);
  if (rsaP.empty()) {
    return setError("RsaPkcs1Encrypt failed");
  }
  std::transform(rsaP.begin(), rsaP.end(), rsaP.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  // 3) Build the signed URL.  Actual implementation uses
  // SignatureAndroidParams with KuGouSaltKind::Lite (the concept/lite salt).
  // The appid must be 3116 for the concept edition; 1005 picks the standard
  // salt and produces error_code 20010. Notably NO `plat` parameter.
  const auto clienttime = std::to_string(std::time(nullptr));
  // The old comment claimed this endpoint needs SignatureRegisterParams
  // (salt="1014"); that was incorrect — the real traffic uses
  // SignatureAndroidParams + lite salt + appid=3116.
  const auto androidMid = AndroidMidForDevice(device);
  std::unordered_map<std::string, std::string> params = {
      {"appid",     profile.appid},
      {"clientver", profile.clientver},
      {"clienttime", clienttime},
      {"part",      "1"},
      {"platid",    "1"},
      {"p",         rsaP},
      {"mid",       androidMid},
      {"uuid",      "-"},
      {"dfid",      device.dfid.empty() ? "-" : device.dfid},
  };
  if (!userId.empty() && userId != "0") params["userid"] = userId;
  if (!token.empty()) params["token"] = token;
  params["signature"] = SignatureAndroidParams(params, aes.data, profile.saltKind);

  std::ostringstream urlStream;
  urlStream << "https://userservice.kugou.com/risk/v2/r_register_dev?";
  bool first = true;
  for (const auto& [k, v] : params) {
    if (!first) urlStream << "&";
    urlStream << k << "=" << UrlEncode(v);
    first = false;
  }

  // 4) POST with the AES ciphertext as body. Match useAxios headers exactly:
  //    User-Agent, dfid, clienttime, mid, kg-rc, kg-thash, kg-rec, kg-rf.
  const auto result = httpPost_(
      urlStream.str(),
      aes.data,
      {
          {"User-Agent",   "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi"},
          {"dfid",         device.dfid.empty() ? "-" : device.dfid},
          {"clienttime",   clienttime},
          {"mid",          androidMid},
          {"kg-rc",        "1"},
          {"kg-thash",     "5d816a0"},
          {"kg-rec",       "1"},
          {"kg-rf",        "B9EDA08A64250DEFFBCADDEE00F8F25F"},
      });

  if (!result.error.empty()) return setError(result.error);
  if (result.statusCode < 200 || result.statusCode >= 300) {
    return setError("HTTP " + std::to_string(result.statusCode));
  }

  // 5) Response body is either:
  //   (a) AES-encrypted base64 of {status:1, data:{dfid:"..."}} — happy path
  //   (b) Plain JSON {status:0, error_code:...} — error path
  // Try plaintext JSON parse first; if status missing, try AES-decrypt.
  nlohmann::json decoded;
  bool parsedAsPlain = false;
  try {
    decoded = nlohmann::json::parse(result.body);
    if (decoded.is_object() && decoded.contains("status")) {
      parsedAsPlain = true;
    }
  } catch (...) {
    // not JSON — fall through to AES decrypt
  }

  if (!parsedAsPlain) {
    // KuGou's risk service returns raw binary AES bytes. PlaylistAesDecrypt
    // expects base64 — match MakcRe's `res.body.toString('base64')`.
    const auto base64Body = Base64EncodeBytes(result.body);
    const auto plaintext = PlaylistAesDecrypt(base64Body, aes.key);
    if (plaintext.empty()) {
      return setError("Failed to AES-decrypt registration response (body bytes=" +
                      std::to_string(result.body.size()) + ")");
    }
    try {
      decoded = nlohmann::json::parse(plaintext);
    } catch (const nlohmann::json::exception& e) {
      return setError(std::string("Bad JSON after AES decrypt: ") + e.what());
    }
  }

  if (decoded.value("status", 0) != 1) {
    const auto msg = decoded.value("error_msg",
                     decoded.value("error", std::string("status != 1")));
    const auto errcode = decoded.value("error_code", 0);
    return setError(msg + " (error_code=" + std::to_string(errcode) + ")");
  }

  if (!decoded.contains("data") || !decoded["data"].is_object()) {
    return setError("Registration response missing data");
  }
  const auto& data = decoded["data"];
  if (!data.contains("dfid") || !data["dfid"].is_string()) {
    return setError("Registration response missing dfid");
  }
  return data["dfid"].get<std::string>();
}

}  // namespace echo::core

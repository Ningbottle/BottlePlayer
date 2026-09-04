#include "echo/core/Crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <random>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace echo::core {

std::string CalculateMd5(const std::string& input) {
  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_HASH_HANDLE hashHandle = nullptr;
  std::string resultHex;

  if (BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_MD5_ALGORITHM, nullptr, 0) >= 0) {
    DWORD hashObjSize = 0;
    DWORD resultDataSize = 0;
    if (BCryptGetProperty(
            algHandle,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hashObjSize),
            sizeof(hashObjSize),
            &resultDataSize,
            0) >= 0) {
      std::vector<UCHAR> hashObj(hashObjSize);
      if (BCryptCreateHash(
              algHandle,
              &hashHandle,
              hashObj.data(),
              hashObjSize,
              nullptr,
              0,
              0) >= 0) {
        if (BCryptHashData(
                hashHandle,
                reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                static_cast<ULONG>(input.size()),
                0) >= 0) {
          UCHAR hashVal[16] = {0};
          if (BCryptFinishHash(hashHandle, hashVal, sizeof(hashVal), 0) >= 0) {
            char hex[33];
            for (int i = 0; i < 16; ++i) {
              sprintf_s(hex + i * 2, 3, "%02x", hashVal[i]);
            }
            resultHex = std::string(hex, 32);
          }
        }
        BCryptDestroyHash(hashHandle);
      }
    }
    BCryptCloseAlgorithmProvider(algHandle, 0);
  }
  return resultHex;
}

std::string SignatureWebParams(const std::unordered_map<std::string, std::string>& params) {
  const std::string salt = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt";
  std::vector<std::string> keys;
  keys.reserve(params.size());
  for (const auto& [key, _] : params) {
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());

  std::string paramsString;
  for (const auto& key : keys) {
    paramsString += key + "=" + params.at(key);
  }
  return CalculateMd5(salt + paramsString + salt);
}

std::string SignatureRegisterParams(
    const std::unordered_map<std::string, std::string>& params) {
  // Match MakcRe/helper.js signatureRegisterParams:
  //   md5("1014" + sorted(values).join("") + "1014")
  std::vector<std::string> values;
  values.reserve(params.size());
  for (const auto& [_, v] : params) {
    values.push_back(v);
  }
  std::sort(values.begin(), values.end());
  std::string joined;
  for (const auto& v : values) joined += v;
  return CalculateMd5("1014" + joined + "1014");
}

const char* AndroidSalt(KuGouSaltKind kind) {
  return kind == KuGouSaltKind::Lite
             ? "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA"
             : "OIlwieks28dk2k092lksi2UIkp";
}

const char* KeySalt(KuGouSaltKind kind) {
  return kind == KuGouSaltKind::Lite
             ? "185672dd44712f60bb1736df5a377e82"
             : "57ae12eb6890223e355ccfcb74edf70d";
}

std::string SignatureAndroidParams(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& data,
    KuGouSaltKind saltKind) {
  const std::string salt = AndroidSalt(saltKind);
  std::vector<std::string> keys;
  keys.reserve(params.size());
  for (const auto& [key, _] : params) {
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());

  std::string paramsString;
  for (const auto& key : keys) {
    paramsString += key + "=" + params.at(key);
  }
  return CalculateMd5(salt + paramsString + data + salt);
}

// KuGou RSA-1024 public key in Base64 DER (SubjectPublicKeyInfo / X.509 SPKI).
// Matches `publicRasKey` in server/util/crypto.js (stripped of PEM headers).
static const char kKuGouPublicKeyB64[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDIAG7QOELSYoIJvTFJhMpe1s/g"
    "bjDJX51HBNnEl5HXqTW6lQ7LC8jr9fWZTwusknp+sVGzwd40MwP6U5yDE27M/X1+"
    "UR4tvOGOqp94TJtQ1EPnWGWXngpeIW5GxoQGao1rmYWAu6oi1z9XkChrsUdC6DJE"
    "5E221wf/4WLFxwAtRQIDAQAB";
static const char kKuGouLitePublicKeyB64[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDECi0Np2UR87scwrvTr72L6oO01"
    "rBbbBPriSDFPxr3Z5syug0O24QyQO8bg27+0+4kBzTBTBOZ/WWU0WryL1JSXRTXL"
    "gFVxtzIY41Pe7lPOgsfTCn5kZcvKhYKJesKnnJDNr5/abvTGf+rHG3YRwsCHcQ08"
    "/q6ifSioBszvb3QiwIDAQAB";

namespace {

BCRYPT_KEY_HANDLE GetKuGouPublicKey(KuGouSaltKind saltKind = KuGouSaltKind::Standard) {
  const char* keyB64 = saltKind == KuGouSaltKind::Lite
      ? kKuGouLitePublicKeyB64
      : kKuGouPublicKeyB64;
  DWORD derLen = 0;
  if (!CryptStringToBinaryA(keyB64, 0, CRYPT_STRING_BASE64, nullptr, &derLen, nullptr, nullptr)) {
    return nullptr;
  }
  std::vector<BYTE> derBytes(derLen);
  if (!CryptStringToBinaryA(keyB64, 0, CRYPT_STRING_BASE64, derBytes.data(), &derLen, nullptr, nullptr)) {
    return nullptr;
  }

  CERT_PUBLIC_KEY_INFO* spkiPtr = nullptr;
  DWORD spkiBufLen = 0;
  if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, derBytes.data(), derLen, CRYPT_DECODE_ALLOC_FLAG, nullptr, reinterpret_cast<void**>(&spkiPtr), &spkiBufLen)) {
    return nullptr;
  }

  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  BOOL importOk = CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, spkiPtr, 0, nullptr, &keyHandle);
  ::LocalFree(spkiPtr);

  if (!importOk) {
    return nullptr;
  }
  return keyHandle;
}

std::string Base64Encode(const std::vector<BYTE>& data) {
  DWORD b64Len = 0;
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64Len)) {
    return {};
  }
  std::string b64Str(b64Len, '\0');
  if (!CryptBinaryToStringA(data.data(), static_cast<DWORD>(data.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64Str[0], &b64Len)) {
    return {};
  }
  while (!b64Str.empty() && (b64Str.back() == '\0' || b64Str.back() == '\r' || b64Str.back() == '\n')) {
    b64Str.pop_back();
  }
  return b64Str;
}

std::vector<BYTE> Base64Decode(const std::string& b64Str) {
  DWORD decodedLen = 0;
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, nullptr, &decodedLen, nullptr, nullptr)) {
    return {};
  }
  std::vector<BYTE> decoded(decodedLen);
  if (!CryptStringToBinaryA(b64Str.data(), static_cast<DWORD>(b64Str.size()), CRYPT_STRING_BASE64, decoded.data(), &decodedLen, nullptr, nullptr)) {
    return {};
  }
  return decoded;
}

std::string GenerateRandom6Char() {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
  std::string s;
  s.reserve(6);
  thread_local std::mt19937 gen{std::random_device{}()};
  std::uniform_int_distribution<> dis(0, 25);
  for (int i = 0; i < 6; ++i) {
    s += alphabet[dis(gen)];
  }
  return s;
}

} // namespace

std::string RsaRawEncrypt(const std::string& jsonPayload) {
  BCRYPT_KEY_HANDLE keyHandle = GetKuGouPublicKey();
  if (!keyHandle) return {};

  constexpr ULONG kKeyBytes = 128;
  std::vector<BYTE> padded(kKeyBytes, 0);
  const auto payloadSize = static_cast<ULONG>(jsonPayload.size());
  if (payloadSize > kKeyBytes) {
    BCryptDestroyKey(keyHandle);
    return {};
  }
  std::memcpy(padded.data() + (kKeyBytes - payloadSize), jsonPayload.data(), payloadSize);

  ULONG cipherLen = 0;
  NTSTATUS status = BCryptEncrypt(
      keyHandle, padded.data(), kKeyBytes,
      nullptr, nullptr, 0,
      nullptr, 0, &cipherLen, BCRYPT_PAD_NONE);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    return {};
  }

  std::vector<BYTE> cipher(cipherLen);
  status = BCryptEncrypt(
      keyHandle, padded.data(), kKeyBytes,
      nullptr, nullptr, 0,
      cipher.data(), cipherLen, &cipherLen, BCRYPT_PAD_NONE);
  BCryptDestroyKey(keyHandle);

  if (!BCRYPT_SUCCESS(status)) return {};

  std::ostringstream ss;
  ss << std::uppercase << std::hex << std::setfill('0');
  for (ULONG i = 0; i < cipherLen; ++i) {
    ss << std::setw(2) << static_cast<int>(cipher[i]);
  }
  return ss.str();
}

std::string RsaRawEncryptRef(const std::string& payload) {
  BCRYPT_KEY_HANDLE keyHandle = GetKuGouPublicKey();
  if (!keyHandle) return {};

  constexpr ULONG kKeyBytes = 128;
  const auto payloadSize = static_cast<ULONG>(payload.size());
  if (payloadSize > kKeyBytes) {
    BCryptDestroyKey(keyHandle);
    return {};
  }
  // 参考仓 rsaRawEncrypt：payload 左对齐，右侧补零。
  std::vector<BYTE> padded(kKeyBytes, 0);
  std::memcpy(padded.data(), payload.data(), payloadSize);

  ULONG cipherLen = 0;
  NTSTATUS status = BCryptEncrypt(
      keyHandle, padded.data(), kKeyBytes,
      nullptr, nullptr, 0,
      nullptr, 0, &cipherLen, BCRYPT_PAD_NONE);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    return {};
  }

  std::vector<BYTE> cipher(cipherLen);
  status = BCryptEncrypt(
      keyHandle, padded.data(), kKeyBytes,
      nullptr, nullptr, 0,
      cipher.data(), cipherLen, &cipherLen, BCRYPT_PAD_NONE);
  BCryptDestroyKey(keyHandle);
  if (!BCRYPT_SUCCESS(status)) return {};

  std::ostringstream ss2;
  ss2 << std::hex << std::setfill('0');
  for (ULONG i = 0; i < cipherLen; ++i) {
    ss2 << std::setw(2) << static_cast<int>(cipher[i]);
  }
  return ss2.str();
}

std::string SignParamsKey(const std::string& data,
                          const std::string& appid,
                          const std::string& clientver,
                          KuGouSaltKind saltKind) {
  const std::string salt = AndroidSalt(saltKind);
  return CalculateMd5(appid + salt + clientver + data);
}

std::string SignKey(const std::string& hash,
                    const std::string& mid,
                    const std::string& userid,
                    const std::string& appid,
                    KuGouSaltKind saltKind) {
  const std::string salt = KeySalt(saltKind);
  return CalculateMd5(hash + salt + appid + mid + (userid.empty() ? "0" : userid));
}

// Hex → decimal string (arbitrary precision via manual base conversion). Used
// to convert a 32-char md5 digest into the 38-39 digit decimal mid that
// KuGou's Android-family clients send.
std::string HexStringToDecimalString(const std::string& hex) {
  // digits[] holds decimal digits in little-endian order (digits[0] is the
  // ones place). We process the hex string left-to-right, repeatedly doing
  // `result = result * 16 + nibble`.
  std::vector<unsigned char> digits = {0};
  for (char c : hex) {
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;  // skip non-hex chars defensively

    // multiply digits by 16
    unsigned int carry = 0;
    for (auto& d : digits) {
      unsigned int x = d * 16u + carry;
      d = static_cast<unsigned char>(x % 10u);
      carry = x / 10u;
    }
    while (carry > 0) {
      digits.push_back(static_cast<unsigned char>(carry % 10u));
      carry /= 10u;
    }

    // add v
    carry = static_cast<unsigned int>(v);
    for (auto& d : digits) {
      if (carry == 0) break;
      unsigned int x = d + carry;
      d = static_cast<unsigned char>(x % 10u);
      carry = x / 10u;
    }
    while (carry > 0) {
      digits.push_back(static_cast<unsigned char>(carry % 10u));
      carry /= 10u;
    }
  }
  // digits is little-endian; reverse to produce the printable string.
  std::string result;
  result.reserve(digits.size());
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    result.push_back(static_cast<char>('0' + *it));
  }
  return result.empty() ? "0" : result;
}

std::string CalculateAndroidMid(const std::string& input) {
  // MakcRe util/util.js calculateMid(): md5(input).hex → base16-BigInt → base10
  const std::string digest = CalculateMd5(input);  // 32 lowercase hex chars
  return HexStringToDecimalString(digest);
}

std::string RsaPkcs1Encrypt(const std::string& payload) {
  return RsaPkcs1Encrypt(payload, KuGouSaltKind::Standard);
}

std::string RsaPkcs1Encrypt(const std::string& payload, KuGouSaltKind saltKind) {
  BCRYPT_KEY_HANDLE keyHandle = GetKuGouPublicKey(saltKind);
  if (!keyHandle) return {};

  BCRYPT_PKCS1_PADDING_INFO padInfo = { nullptr };
  ULONG cipherLen = 0;
  NTSTATUS status = BCryptEncrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
      static_cast<ULONG>(payload.size()),
      &padInfo,
      nullptr,
      0,
      nullptr,
      0,
      &cipherLen,
      BCRYPT_PAD_PKCS1);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    return {};
  }

  std::vector<BYTE> cipher(cipherLen);
  status = BCryptEncrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())),
      static_cast<ULONG>(payload.size()),
      &padInfo,
      nullptr,
      0,
      cipher.data(),
      cipherLen,
      &cipherLen,
      BCRYPT_PAD_PKCS1);

  BCryptDestroyKey(keyHandle);

  if (!BCRYPT_SUCCESS(status)) return {};

  std::ostringstream ss;
  ss << std::uppercase << std::hex << std::setfill('0');
  for (ULONG i = 0; i < cipherLen; ++i) {
    ss << std::setw(2) << static_cast<int>(cipher[i]);
  }
  return ss.str();
}

AesKeyPair PlaylistAesEncrypt(const std::string& plaintext) {
  std::string keySeed = GenerateRandom6Char();
  std::string md5 = CalculateMd5(keySeed);
  std::string encryptKey = md5.substr(0, 16);
  std::string ivStr = md5.substr(16, 16);
  std::vector<BYTE> ivBytes(ivStr.begin(), ivStr.end());

  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) return {};

  status = BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  DWORD keyObjSize = 0;
  DWORD cbData = 0;
  status = BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&keyObjSize), sizeof(keyObjSize), &cbData, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> keyObj(keyObjSize);
  status = BCryptGenerateSymmetricKey(algHandle, &keyHandle, keyObj.data(), keyObjSize, reinterpret_cast<PUCHAR>(const_cast<char*>(encryptKey.data())), 16, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  ULONG cipherLen = 0;
  status = BCryptEncrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
      static_cast<ULONG>(plaintext.size()),
      nullptr,
      ivBytes.data(),
      static_cast<ULONG>(ivBytes.size()),
      nullptr,
      0,
      &cipherLen,
      BCRYPT_BLOCK_PADDING);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> cipher(cipherLen);
  std::memcpy(ivBytes.data(), ivStr.data(), 16);

  status = BCryptEncrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
      static_cast<ULONG>(plaintext.size()),
      nullptr,
      ivBytes.data(),
      static_cast<ULONG>(ivBytes.size()),
      cipher.data(),
      cipherLen,
      &cipherLen,
      BCRYPT_BLOCK_PADDING);

  BCryptDestroyKey(keyHandle);
  BCryptCloseAlgorithmProvider(algHandle, 0);

  if (!BCRYPT_SUCCESS(status)) return {};

  std::string base64Str = Base64Encode(cipher);
  return AesKeyPair{keySeed, base64Str};
}

std::string AesCbcEncryptBase64(const std::string& plaintext,
                                const std::string& key,
                                const std::string& iv) {
  if (key.empty() || iv.empty()) return {};
  std::vector<BYTE> ivBytes(iv.begin(), iv.end());

  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) return {};

  status = BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  DWORD keyObjSize = 0;
  DWORD cbData = 0;
  status = BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&keyObjSize), sizeof(keyObjSize), &cbData, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> keyObj(keyObjSize);
  status = BCryptGenerateSymmetricKey(algHandle, &keyHandle, keyObj.data(), keyObjSize,
                                      reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),
                                      static_cast<ULONG>(key.size()), 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  ULONG cipherLen = 0;
  status = BCryptEncrypt(keyHandle,
                         reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
                         static_cast<ULONG>(plaintext.size()), nullptr,
                         ivBytes.data(), static_cast<ULONG>(ivBytes.size()),
                         nullptr, 0, &cipherLen, BCRYPT_BLOCK_PADDING);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> cipher(cipherLen);
  std::memcpy(ivBytes.data(), iv.data(), iv.size());
  status = BCryptEncrypt(keyHandle,
                         reinterpret_cast<PUCHAR>(const_cast<char*>(plaintext.data())),
                         static_cast<ULONG>(plaintext.size()), nullptr,
                         ivBytes.data(), static_cast<ULONG>(ivBytes.size()),
                         cipher.data(), cipherLen, &cipherLen, BCRYPT_BLOCK_PADDING);
  BCryptDestroyKey(keyHandle);
  BCryptCloseAlgorithmProvider(algHandle, 0);
  if (!BCRYPT_SUCCESS(status)) return {};
  return Base64Encode(cipher);
}

std::string Base64EncodeBytes(const std::string& rawBytes) {
  std::vector<BYTE> bytes(rawBytes.begin(), rawBytes.end());
  return Base64Encode(bytes);
}

std::string PlaylistAesDecrypt(const std::string& base64Cipher, const std::string& keySeed) {
  std::vector<BYTE> cipher = Base64Decode(base64Cipher);
  if (cipher.empty()) return {};

  std::string md5 = CalculateMd5(keySeed);
  std::string encryptKey = md5.substr(0, 16);
  std::string ivStr = md5.substr(16, 16);
  std::vector<BYTE> ivBytes(ivStr.begin(), ivStr.end());

  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) return {};

  status = BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  DWORD keyObjSize = 0;
  DWORD cbData = 0;
  status = BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&keyObjSize), sizeof(keyObjSize), &cbData, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> keyObj(keyObjSize);
  status = BCryptGenerateSymmetricKey(algHandle, &keyHandle, keyObj.data(), keyObjSize, reinterpret_cast<PUCHAR>(const_cast<char*>(encryptKey.data())), 16, 0);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  ULONG plainLen = 0;
  status = BCryptDecrypt(
      keyHandle,
      cipher.data(),
      static_cast<ULONG>(cipher.size()),
      nullptr,
      ivBytes.data(),
      static_cast<ULONG>(ivBytes.size()),
      nullptr,
      0,
      &plainLen,
      BCRYPT_BLOCK_PADDING);
  if (!BCRYPT_SUCCESS(status)) {
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return {};
  }

  std::vector<BYTE> plain(plainLen);
  std::memcpy(ivBytes.data(), ivStr.data(), 16);

  status = BCryptDecrypt(
      keyHandle,
      cipher.data(),
      static_cast<ULONG>(cipher.size()),
      nullptr,
      ivBytes.data(),
      static_cast<ULONG>(ivBytes.size()),
      plain.data(),
      plainLen,
      &plainLen,
      BCRYPT_BLOCK_PADDING);

  BCryptDestroyKey(keyHandle);
  BCryptCloseAlgorithmProvider(algHandle, 0);

  if (!BCRYPT_SUCCESS(status)) return {};

  return std::string(reinterpret_cast<char*>(plain.data()), plainLen);
}

}  // namespace echo::core

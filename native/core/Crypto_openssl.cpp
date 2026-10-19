#include "echo/core/Crypto.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

#include "echo/core/PortableBase64.h"

namespace echo::core {
namespace {

using PkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using CipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

constexpr char kKuGouPublicKeyB64[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDIAG7QOELSYoIJvTFJhMpe1s/g"
    "bjDJX51HBNnEl5HXqTW6lQ7LC8jr9fWZTwusknp+sVGzwd40MwP6U5yDE27M/X1+"
    "UR4tvOGOqp94TJtQ1EPnWGWXngpeIW5GxoQGao1rmYWAu6oi1z9XkChrsUdC6DJE"
    "5E221wf/4WLFxwAtRQIDAQAB";
constexpr char kKuGouLitePublicKeyB64[] =
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDECi0Np2UR87scwrvTr72L6oO01"
    "rBbbBPriSDFPxr3Z5syug0O24QyQO8bg27+0+4kBzTBTBOZ/WWU0WryL1JSXRTXL"
    "gFVxtzIY41Pe7lPOgsfTCn5kZcvKhYKJesKnnJDNr5/abvTGf+rHG3YRwsCHcQ08"
    "/q6ifSioBszvb3QiwIDAQAB";

PkeyPtr LoadKuGouPublicKey(KuGouSaltKind saltKind) {
  const char* encoded = saltKind == KuGouSaltKind::Lite
      ? kKuGouLitePublicKeyB64 : kKuGouPublicKeyB64;
  const auto der = portable::Base64Decode(encoded);
  if (der.empty()) return {nullptr, EVP_PKEY_free};
  const unsigned char* cursor = der.data();
  EVP_PKEY* key = d2i_PUBKEY(nullptr, &cursor, static_cast<long>(der.size()));
  return {key, EVP_PKEY_free};
}

std::string ToHex(const unsigned char* bytes, std::size_t size, bool uppercase) {
  static constexpr char kLower[] = "0123456789abcdef";
  static constexpr char kUpper[] = "0123456789ABCDEF";
  const char* digits = uppercase ? kUpper : kLower;
  std::string output(size * 2, '\0');
  for (std::size_t i = 0; i < size; ++i) {
    output[2 * i] = digits[bytes[i] >> 4];
    output[2 * i + 1] = digits[bytes[i] & 0x0f];
  }
  return output;
}

std::vector<unsigned char> FromHex(const std::string& value) {
  if (value.empty() || value.size() % 2 != 0) return {};
  std::vector<unsigned char> output(value.size() / 2);
  for (std::size_t i = 0; i < output.size(); ++i) {
    const auto nibble = [](char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const int high = nibble(value[2 * i]);
    const int low = nibble(value[2 * i + 1]);
    if (high < 0 || low < 0) return {};
    output[i] = static_cast<unsigned char>((high << 4) | low);
  }
  return output;
}

const EVP_CIPHER* AesCbcCipher(std::size_t keySize) {
  switch (keySize) {
    case 16: return EVP_aes_128_cbc();
    case 24: return EVP_aes_192_cbc();
    case 32: return EVP_aes_256_cbc();
    default: return nullptr;
  }
}

std::vector<unsigned char> AesCbcEncrypt(
    const std::string& plaintext, const std::string& key, const std::string& iv) {
  const auto* cipher = AesCbcCipher(key.size());
  if (!cipher || iv.size() != 16 || plaintext.size() > static_cast<std::size_t>(INT_MAX)) {
    return {};
  }
  CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!context || EVP_EncryptInit_ex(
          context.get(), cipher, nullptr,
          reinterpret_cast<const unsigned char*>(key.data()),
          reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
    return {};
  }
  std::vector<unsigned char> encrypted(plaintext.size() + EVP_CIPHER_block_size(cipher));
  int written = 0;
  int final = 0;
  if (EVP_EncryptUpdate(
          context.get(), encrypted.data(), &written,
          reinterpret_cast<const unsigned char*>(plaintext.data()),
          static_cast<int>(plaintext.size())) != 1 ||
      EVP_EncryptFinal_ex(context.get(), encrypted.data() + written, &final) != 1) {
    return {};
  }
  encrypted.resize(static_cast<std::size_t>(written + final));
  return encrypted;
}

std::string RawRsaEncrypt(const std::string& payload, KuGouSaltKind saltKind,
                          bool leftAligned, bool uppercase) {
  if (payload.size() > 128) return {};
  auto key = LoadKuGouPublicKey(saltKind);
  if (!key) return {};
  std::array<unsigned char, 128> padded{};
  const auto offset = leftAligned ? 0 : padded.size() - payload.size();
  std::memcpy(padded.data() + offset, payload.data(), payload.size());

  PkeyCtxPtr context(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  std::size_t outputSize = 0;
  if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0 ||
      EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_NO_PADDING) <= 0 ||
      EVP_PKEY_encrypt(context.get(), nullptr, &outputSize, padded.data(), padded.size()) <= 0) {
    return {};
  }
  std::vector<unsigned char> encrypted(outputSize);
  if (EVP_PKEY_encrypt(context.get(), encrypted.data(), &outputSize,
                       padded.data(), padded.size()) <= 0) {
    return {};
  }
  return ToHex(encrypted.data(), outputSize, uppercase);
}

std::string Pkcs1RsaEncrypt(const std::string& payload, KuGouSaltKind saltKind) {
  if (payload.size() > 117) return {};
  auto key = LoadKuGouPublicKey(saltKind);
  if (!key) return {};
  PkeyCtxPtr context(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
  std::size_t outputSize = 0;
  if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0 ||
      EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_PADDING) <= 0 ||
      EVP_PKEY_encrypt(context.get(), nullptr, &outputSize,
                       reinterpret_cast<const unsigned char*>(payload.data()),
                       payload.size()) <= 0) {
    return {};
  }
  std::vector<unsigned char> encrypted(outputSize);
  if (EVP_PKEY_encrypt(
          context.get(), encrypted.data(), &outputSize,
          reinterpret_cast<const unsigned char*>(payload.data()), payload.size()) <= 0) {
    return {};
  }
  return ToHex(encrypted.data(), outputSize, true);
}

std::string EncryptBase64(const std::vector<unsigned char>& bytes) {
  return portable::Base64Encode(bytes.data(), bytes.size());
}

std::vector<unsigned char> DecryptBase64(const std::string& value) {
  return portable::Base64Decode(value);
}

}  // namespace

std::string CalculateMd5(const std::string& input) {
  MdCtxPtr context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (!context || EVP_DigestInit_ex(context.get(), EVP_md5(), nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), input.data(), input.size()) != 1 ||
      EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != 16) {
    return {};
  }
  return ToHex(digest.data(), length, false);
}

std::string SignatureWebParams(const std::unordered_map<std::string, std::string>& params) {
  const std::string salt = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt";
  std::vector<std::string> keys;
  keys.reserve(params.size());
  for (const auto& [key, _] : params) keys.push_back(key);
  std::sort(keys.begin(), keys.end());
  std::string joined;
  for (const auto& key : keys) joined += key + "=" + params.at(key);
  return CalculateMd5(salt + joined + salt);
}

std::string SignatureRegisterParams(
    const std::unordered_map<std::string, std::string>& params) {
  std::vector<std::string> values;
  for (const auto& [_, value] : params) values.push_back(value);
  std::sort(values.begin(), values.end());
  std::string joined;
  for (const auto& value : values) joined += value;
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
    const std::string& data, KuGouSaltKind saltKind) {
  std::vector<std::string> keys;
  keys.reserve(params.size());
  for (const auto& [key, _] : params) keys.push_back(key);
  std::sort(keys.begin(), keys.end());
  std::string joined;
  for (const auto& key : keys) joined += key + "=" + params.at(key);
  const std::string salt = AndroidSalt(saltKind);
  return CalculateMd5(salt + joined + data + salt);
}

std::string SignParamsKey(const std::string& data, const std::string& appid,
                          const std::string& clientver, KuGouSaltKind saltKind) {
  return CalculateMd5(appid + AndroidSalt(saltKind) + clientver + data);
}

std::string SignKey(const std::string& hash, const std::string& mid,
                    const std::string& userid, const std::string& appid,
                    KuGouSaltKind saltKind) {
  return CalculateMd5(hash + KeySalt(saltKind) + appid + mid +
                      (userid.empty() ? "0" : userid));
}

std::string RsaRawEncrypt(const std::string& payload) {
  return RawRsaEncrypt(payload, KuGouSaltKind::Standard, false, true);
}

std::string RsaRawEncryptRef(const std::string& payload) {
  return RsaRawEncryptRef(payload, KuGouSaltKind::Standard);
}

std::string RsaRawEncryptRef(const std::string& payload, KuGouSaltKind saltKind) {
  return RawRsaEncrypt(payload, saltKind, true, false);
}

std::string RsaPkcs1Encrypt(const std::string& payload) {
  return RsaPkcs1Encrypt(payload, KuGouSaltKind::Standard);
}

std::string RsaPkcs1Encrypt(const std::string& payload, KuGouSaltKind saltKind) {
  return Pkcs1RsaEncrypt(payload, saltKind);
}

std::string GenerateRandom6Char() {
  static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
  std::string output;
  output.reserve(6);
  while (output.size() < 6) {
    std::array<unsigned char, 16> random{};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) return {};
    for (const auto byte : random) {
      if (byte >= 234) continue;
      output.push_back(alphabet[byte % 26]);
      if (output.size() == 6) break;
    }
  }
  return output;
}

std::string PlaylistAesDecrypt(const std::string& base64Cipher,
                               const std::string& keySeed) {
  const auto cipher = DecryptBase64(base64Cipher);
  if (cipher.empty()) return {};
  const auto md5 = CalculateMd5(keySeed);
  return AesCbcDecryptHex(ToHex(cipher.data(), cipher.size(), false),
                          md5.substr(0, 16), md5.substr(16, 16));
}

AesKeyPair PlaylistAesEncrypt(const std::string& plaintext) {
  const auto seed = GenerateRandom6Char();
  if (seed.empty()) return {};
  const auto md5 = CalculateMd5(seed);
  const auto encrypted = AesCbcEncrypt(plaintext, md5.substr(0, 16), md5.substr(16, 16));
  return encrypted.empty() ? AesKeyPair{} : AesKeyPair{seed, EncryptBase64(encrypted)};
}

std::string AesCbcEncryptBase64(const std::string& plaintext,
                                const std::string& key, const std::string& iv) {
  return EncryptBase64(AesCbcEncrypt(plaintext, key, iv));
}

std::string AesCbcEncryptHex(const std::string& plaintext, const std::string& key,
                             const std::string& iv) {
  if (key.size() != 16 && key.size() != 24 && key.size() != 32) return {};
  if (iv.size() != 16) return {};
  const auto encrypted = AesCbcEncrypt(plaintext, key, iv);
  return encrypted.empty() ? std::string{} : ToHex(encrypted.data(), encrypted.size(), false);
}

std::string AesCbcDecryptHex(const std::string& hexCipher, const std::string& key,
                             const std::string& iv) {
  if ((key.size() != 16 && key.size() != 24 && key.size() != 32) ||
      iv.size() != 16) return {};
  const auto cipher = FromHex(hexCipher);
  const auto* algorithm = AesCbcCipher(key.size());
  if (cipher.empty() || cipher.size() > static_cast<std::size_t>(INT_MAX)) return {};
  CipherCtxPtr context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!context || EVP_DecryptInit_ex(
          context.get(), algorithm, nullptr,
          reinterpret_cast<const unsigned char*>(key.data()),
          reinterpret_cast<const unsigned char*>(iv.data())) != 1) return {};
  std::vector<unsigned char> plaintext(cipher.size());
  int written = 0;
  int final = 0;
  if (EVP_DecryptUpdate(context.get(), plaintext.data(), &written, cipher.data(),
                        static_cast<int>(cipher.size())) != 1 ||
      EVP_DecryptFinal_ex(context.get(), plaintext.data() + written, &final) != 1) return {};
  return std::string(reinterpret_cast<const char*>(plaintext.data()), written + final);
}

std::string Base64EncodeBytes(const std::string& rawBytes) {
  return portable::Base64Encode(
      reinterpret_cast<const unsigned char*>(rawBytes.data()), rawBytes.size());
}

std::string HexStringToDecimalString(const std::string& hex) {
  std::vector<unsigned char> digits{0};
  for (const char c : hex) {
    int value = -1;
    if (c >= '0' && c <= '9') value = c - '0';
    else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
    if (value < 0) continue;
    unsigned int carry = 0;
    for (auto& digit : digits) {
      const unsigned int product = digit * 16u + carry;
      digit = static_cast<unsigned char>(product % 10u);
      carry = product / 10u;
    }
    while (carry) {
      digits.push_back(static_cast<unsigned char>(carry % 10u));
      carry /= 10u;
    }
    carry = static_cast<unsigned int>(value);
    for (auto& digit : digits) {
      if (!carry) break;
      const unsigned int sum = digit + carry;
      digit = static_cast<unsigned char>(sum % 10u);
      carry = sum / 10u;
    }
    while (carry) {
      digits.push_back(static_cast<unsigned char>(carry % 10u));
      carry /= 10u;
    }
  }
  std::string result;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    result.push_back(static_cast<char>('0' + *it));
  }
  return result.empty() ? "0" : result;
}

std::string CalculateAndroidMid(const std::string& input) {
  return HexStringToDecimalString(CalculateMd5(input));
}

}  // namespace echo::core

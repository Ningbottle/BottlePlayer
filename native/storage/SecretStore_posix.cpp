#include "echo/storage/SecretStore.h"

#include "echo/storage/AppPaths.h"

#include <libsecret/secret.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace echo::storage {
namespace {

constexpr std::size_t kKeyBytes = 32;
constexpr std::size_t kNonceBytes = 12;
constexpr std::size_t kTagBytes = 16;

const SecretSchema kSessionKeySchema = {
    "com.bottlemusic.SessionKey",
    SECRET_SCHEMA_NONE,
    {{"profile", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
};

struct KeyWiper {
  std::array<unsigned char, kKeyBytes>& key;
  ~KeyWiper() { OPENSSL_cleanse(key.data(), key.size()); }
};

std::string Base64Encode(const unsigned char* bytes, std::size_t size) {
  if (size > static_cast<std::size_t>(INT_MAX)) {
    throw std::runtime_error("session_protection_failed");
  }
  std::string result(4 * ((size + 2) / 3) + 1, '\0');
  const int encoded = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(result.data()), bytes, static_cast<int>(size));
  if (encoded < 0) throw std::runtime_error("session_protection_failed");
  result.resize(static_cast<std::size_t>(encoded));
  return result;
}

bool Base64Decode(const std::string& text, std::vector<unsigned char>& output) {
  if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX) ||
      text.size() % 4 != 0) {
    return false;
  }
  output.resize((text.size() / 4) * 3);
  const int decoded = EVP_DecodeBlock(
      output.data(), reinterpret_cast<const unsigned char*>(text.data()),
      static_cast<int>(text.size()));
  if (decoded < 0) return false;
  std::size_t actual = static_cast<std::size_t>(decoded);
  if (text.back() == '=') --actual;
  if (text.size() > 1 && text[text.size() - 2] == '=') --actual;
  output.resize(actual);
  return true;
}

std::filesystem::path KeyPath() {
  return GetAppDataDirectory() / "session.key";
}

bool ReadFileKey(std::array<unsigned char, kKeyBytes>& key) {
  const auto path = KeyPath();
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;

  struct stat info {};
  bool valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
      info.st_uid == geteuid() &&
      (info.st_mode & (S_IRGRP | S_IROTH)) == 0 &&
      info.st_size == static_cast<off_t>(key.size());
  std::size_t readBytes = 0;
  while (valid && readBytes < key.size()) {
    const auto count = read(fd, key.data() + readBytes, key.size() - readBytes);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      valid = false;
      break;
    }
    readBytes += static_cast<std::size_t>(count);
  }
  close(fd);
  return valid && readBytes == key.size();
}

bool CreateFileKey(std::array<unsigned char, kKeyBytes>& key) {
  if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return false;
  const auto path = KeyPath();
  const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    OPENSSL_cleanse(key.data(), key.size());
    if (errno == EEXIST) return ReadFileKey(key);
    return false;
  }

  std::size_t written = 0;
  bool valid = fchmod(fd, 0600) == 0;
  while (valid && written < key.size()) {
    const auto count = write(fd, key.data() + written, key.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      valid = false;
      break;
    }
    written += static_cast<std::size_t>(count);
  }
  if (close(fd) != 0) valid = false;
  if (!valid || written != key.size()) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    OPENSSL_cleanse(key.data(), key.size());
    return false;
  }
  return true;
}

bool LoadFileKey(std::array<unsigned char, kKeyBytes>& key) {
  if (ReadFileKey(key)) return true;
  if (errno != ENOENT) return false;
  return CreateFileKey(key);
}

bool LoadKeyringKey(std::array<unsigned char, kKeyBytes>& key) {
  const auto profile = std::filesystem::absolute(GetAppDataDirectory())
                           .lexically_normal().string();
  GError* error = nullptr;
  gchar* stored = secret_password_lookup_sync(
      &kSessionKeySchema, nullptr, &error, "profile", profile.c_str(), nullptr);
  if (error) {
    g_error_free(error);
    return false;
  }
  if (stored) {
    std::vector<unsigned char> decoded;
    const bool valid = Base64Decode(stored, decoded) && decoded.size() == key.size();
    if (valid) std::copy(decoded.begin(), decoded.end(), key.begin());
    OPENSSL_cleanse(decoded.data(), decoded.size());
    secret_password_free(stored);
    return valid;
  }

  if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return false;
  const std::string encoded = Base64Encode(key.data(), key.size());
  error = nullptr;
  const gboolean storedOk = secret_password_store_sync(
      &kSessionKeySchema, SECRET_COLLECTION_DEFAULT,
      "BottleMusic session encryption key", encoded.c_str(), nullptr, &error,
      "profile", profile.c_str(), nullptr);
  if (error) {
    g_error_free(error);
    OPENSSL_cleanse(key.data(), key.size());
    return false;
  }
  if (!storedOk) {
    OPENSSL_cleanse(key.data(), key.size());
    return false;
  }
  return true;
}

bool LoadKey(std::array<unsigned char, kKeyBytes>& key) {
  const char* backend = std::getenv("ECHO_NATIVE_SECRET_BACKEND");
  if (backend && std::string(backend) == "file") return LoadFileKey(key);
  if (LoadKeyringKey(key)) return true;
  OPENSSL_cleanse(key.data(), key.size());
  return LoadFileKey(key);
}

}  // namespace

std::string ProtectForCurrentUser(const std::string& plaintext) {
  std::array<unsigned char, kKeyBytes> key{};
  KeyWiper wipe{key};
  if (!LoadKey(key)) throw std::runtime_error("session_protection_failed");

  std::array<unsigned char, kNonceBytes> nonce{};
  if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1 ||
      plaintext.size() > static_cast<std::size_t>(INT_MAX)) {
    throw std::runtime_error("session_protection_failed");
  }

  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  if (!context) throw std::runtime_error("session_protection_failed");
  std::vector<unsigned char> encrypted(plaintext.size() + kTagBytes);
  int length = 0;
  int encryptedLength = 0;
  bool valid =
      EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) == 1 &&
      EVP_EncryptInit_ex(context, nullptr, nullptr, key.data(), nonce.data()) == 1 &&
      EVP_EncryptUpdate(context, encrypted.data(), &length,
                        reinterpret_cast<const unsigned char*>(plaintext.data()),
                        static_cast<int>(plaintext.size())) == 1;
  encryptedLength = length;
  if (valid) {
    valid = EVP_EncryptFinal_ex(context, encrypted.data() + encryptedLength, &length) == 1;
    encryptedLength += length;
  }
  if (valid) {
    valid = EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, kTagBytes,
                                encrypted.data() + encryptedLength) == 1;
  }
  EVP_CIPHER_CTX_free(context);
  if (!valid) throw std::runtime_error("session_protection_failed");
  encrypted.resize(static_cast<std::size_t>(encryptedLength) + kTagBytes);

  std::vector<unsigned char> envelope;
  envelope.reserve(nonce.size() + encrypted.size());
  envelope.insert(envelope.end(), nonce.begin(), nonce.end());
  envelope.insert(envelope.end(), encrypted.begin(), encrypted.end());
  return "bmx1:" + Base64Encode(envelope.data(), envelope.size());
}

std::optional<std::string> UnprotectForCurrentUser(const std::string& encoded) {
  constexpr char kPrefix[] = "bmx1:";
  if (encoded.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return std::nullopt;

  std::vector<unsigned char> envelope;
  if (!Base64Decode(encoded.substr(sizeof(kPrefix) - 1), envelope) ||
      envelope.size() < kNonceBytes + kTagBytes) {
    return std::nullopt;
  }

  std::array<unsigned char, kKeyBytes> key{};
  KeyWiper wipe{key};
  if (!LoadKey(key)) return std::nullopt;

  const auto cipherSize = envelope.size() - kNonceBytes - kTagBytes;
  std::string plaintext(cipherSize, '\0');
  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  if (!context) {
    OPENSSL_cleanse(envelope.data(), envelope.size());
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    return std::nullopt;
  }
  int length = 0;
  int plaintextLength = 0;
  bool valid =
      EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, kNonceBytes, nullptr) == 1 &&
      EVP_DecryptInit_ex(context, nullptr, nullptr, key.data(), envelope.data()) == 1 &&
      EVP_DecryptUpdate(context, reinterpret_cast<unsigned char*>(plaintext.data()),
                        &length, envelope.data() + kNonceBytes,
                        static_cast<int>(cipherSize)) == 1;
  plaintextLength = length;
  if (valid) {
    valid = EVP_CIPHER_CTX_ctrl(
        context, EVP_CTRL_GCM_SET_TAG, kTagBytes,
        envelope.data() + kNonceBytes + cipherSize) == 1;
  }
  if (valid) {
    valid = EVP_DecryptFinal_ex(
        context, reinterpret_cast<unsigned char*>(plaintext.data()) + plaintextLength,
        &length) == 1;
    plaintextLength += length;
  }
  EVP_CIPHER_CTX_free(context);
  OPENSSL_cleanse(envelope.data(), envelope.size());
  if (!valid) {
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    return std::nullopt;
  }
  plaintext.resize(static_cast<std::size_t>(plaintextLength));
  return plaintext;
}

}  // namespace echo::storage

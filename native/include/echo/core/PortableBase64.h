#pragma once

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <cstddef>
#include <string>
#include <vector>

namespace echo::core::portable {

inline std::string Base64Encode(const unsigned char* data, std::size_t size) {
  if (size == 0) return {};
  std::string encoded(4 * ((size + 2) / 3), '\0');
  const int length = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(encoded.data()), data,
      static_cast<int>(size));
  if (length < 0) return {};
  encoded.resize(static_cast<std::size_t>(length));
  return encoded;
}

inline std::vector<unsigned char> Base64Decode(const std::string& encoded) {
  if (encoded.empty() || encoded.size() % 4 != 0) return {};
  std::vector<unsigned char> decoded((encoded.size() / 4) * 3);
  const int length = EVP_DecodeBlock(
      decoded.data(), reinterpret_cast<const unsigned char*>(encoded.data()),
      static_cast<int>(encoded.size()));
  if (length < 0) return {};
  std::size_t actual = static_cast<std::size_t>(length);
  if (encoded.back() == '=') --actual;
  if (encoded.size() > 1 && encoded[encoded.size() - 2] == '=') --actual;
  decoded.resize(actual);
  return decoded;
}

inline void SecureZero(void* data, std::size_t size) {
  if (data && size) OPENSSL_cleanse(data, size);
}

}  // namespace echo::core::portable

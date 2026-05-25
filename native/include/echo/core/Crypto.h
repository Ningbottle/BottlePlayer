#pragma once

#include <string>
#include <unordered_map>

namespace echo::core {

// ── MD5 ──────────────────────────────────────────────────────────────────────
std::string CalculateMd5(const std::string& input);

// ── Signature helpers ─────────────────────────────────────────────────────────
std::string SignatureWebParams(const std::unordered_map<std::string, std::string>& params);

std::string SignatureAndroidParams(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& data = "");

// Register-flow signature: md5("1014" + sorted_values_joined + "1014").
// Used by KuGou's risk service endpoints (e.g., /risk/v2/r_register_dev) in
// some app builds. NOTE: MakcRe's `encryptType: 'android'` config might
// actually be wrong for this endpoint — KuGou's signature error_code 20010
// suggests it expects this register-style signature instead.
std::string SignatureRegisterParams(
    const std::unordered_map<std::string, std::string>& params);

// Computes the `key` query parameter used by the cloud / images endpoints:
//   MD5( appid + salt + clientver + data )
// where salt = "OIlwieks28dk2k092lksi2UIkp"
std::string SignParamsKey(const std::string& data,
                          const std::string& appid = "1014",
                          const std::string& clientver = "20000");

// ── RSA (raw / zero-padded, KuGou public key) ─────────────────────────────────
// RSA raw (no-padding, zero-padded) encryption using the KuGou public key.
// Input must be a pre-serialised string <= 128 bytes.
// Returns the hex-encoded 128-byte ciphertext, uppercased.
std::string RsaRawEncrypt(const std::string& jsonPayload);

// RSA PKCS1-v1_5 encryption using the KuGou public key (for cloud endpoints).
// Returns hex-encoded ciphertext, uppercased.
std::string RsaPkcs1Encrypt(const std::string& payload);

// ── AES-CBC / Playlist-style AES ─────────────────────────────────────────────
struct AesKeyPair {
  std::string key;   // random 6-char lowercase string (the seed)
  std::string data;  // Base64-encoded ciphertext
};

// Encrypts `plaintext` (JSON string) with a random 6-char key.
// key derivation: encryptKey = MD5(seed)[0:16], iv = MD5(seed)[16:32]
// Matches JS `playlistAesEncrypt`.
AesKeyPair PlaylistAesEncrypt(const std::string& plaintext);

// Decrypts a `PlaylistAesEncrypt` result.
// Matches JS `playlistAesDecrypt`.
std::string PlaylistAesDecrypt(const std::string& base64Cipher, const std::string& keySeed);

// ── Base64 helpers ────────────────────────────────────────────────────────────
// Encodes raw bytes (typically a binary HTTP response body) as Base64.
// Useful for feeding `PlaylistAesDecrypt`, which expects base64 input.
std::string Base64EncodeBytes(const std::string& rawBytes);

}  // namespace echo::core

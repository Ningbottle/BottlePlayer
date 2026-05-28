#pragma once

#include <string>
#include <unordered_map>

#include "echo/core/KuGouProfile.h"

namespace echo::core {

// ── MD5 ──────────────────────────────────────────────────────────────────────
std::string CalculateMd5(const std::string& input);

// ── Signature helpers ─────────────────────────────────────────────────────────
std::string SignatureWebParams(const std::unordered_map<std::string, std::string>& params);

// Android-style signature: md5(salt + sorted(k=v) + data + salt).
// salt selected by KuGouSaltKind (Lite for concept/lite, Standard otherwise).
std::string SignatureAndroidParams(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& data = "",
    KuGouSaltKind saltKind = KuGouSaltKind::Lite);

// Register-flow signature: md5("1014" + sorted_values_joined + "1014").
// DEPRECATED: no active code path uses this; DeviceRegisterService now uses
// SignatureAndroidParams + KuGouSaltKind::Lite. Kept as archive point.
[[deprecated("no active caller; DeviceRegisterService uses SignatureAndroidParams")]]
std::string SignatureRegisterParams(
    const std::unordered_map<std::string, std::string>& params);

// Computes the `key` query parameter used by the cloud / images endpoints:
//   MD5( appid + salt + clientver + data )
// salt selected by KuGouSaltKind.
std::string SignParamsKey(const std::string& data,
                          const std::string& appid = "1014",
                          const std::string& clientver = "20000",
                          KuGouSaltKind saltKind = KuGouSaltKind::Lite);

// Computes the `key` parameter used by song URL endpoints (v5/v6):
//   MD5( hash + salt + appid + mid + userid )
// salt selected by KuGouSaltKind.
std::string SignKey(const std::string& hash,
                    const std::string& mid,
                    const std::string& userid,
                    const std::string& appid = "3116",
                    KuGouSaltKind saltKind = KuGouSaltKind::Lite);

// ── RSA (raw / zero-padded, KuGou public key) ─────────────────────────────────
// RSA raw (no-padding, zero-padded) encryption using the KuGou public key.
// Input must be a pre-serialised string <= 128 bytes.
// Returns the hex-encoded 128-byte ciphertext, uppercased.
std::string RsaRawEncrypt(const std::string& jsonPayload);

// RSA PKCS1-v1_5 encryption using the KuGou public key (for cloud endpoints).
// Returns hex-encoded ciphertext, uppercased.
std::string RsaPkcs1Encrypt(const std::string& payload);
std::string RsaPkcs1Encrypt(
    const std::string& payload,
    KuGouSaltKind saltKind);

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

// ── Salt accessors (exposed for tests / diagnostics) ─────────────────────────
// Returns the Android-style signature salt string for the given kind.
const char* AndroidSalt(KuGouSaltKind kind);
// Returns the song-URL `key` salt string for the given kind.
const char* KeySalt(KuGouSaltKind kind);

// ── KuGou Android-edition mid derivation ─────────────────────────────────────
// Matches MakcRe util/util.js `calculateMid(str)`:
//   1. digest = md5(str).hex          (32 hex chars)
//   2. interpret digest as a base-16 BigInt
//   3. return the base-10 string representation (≈ 38-39 decimal digits)
//
// KuGou's Android (lite/concept) clients send `mid` as this decimal-bigint
// form; web/m.kugou.com clients send the raw 32-char hex. /v5/url returns
// priv_status:0 when the mid format doesn't match the appid family.
std::string CalculateAndroidMid(const std::string& input);

// Helper: convert a hex string to its base-10 decimal-string representation
// without floating point or fixed-width integers. Used by CalculateAndroidMid
// but exposed for unit tests.
std::string HexStringToDecimalString(const std::string& hex);

}  // namespace echo::core

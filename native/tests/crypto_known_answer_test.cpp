#include "echo/core/Crypto.h"

#include <iostream>
#include <string>
#include <unordered_map>

namespace {

int failures = 0;

void Check(bool passed, const char* name) {
  std::cout << (passed ? "[ok] " : "[FAIL] ") << name << '\n';
  if (!passed) ++failures;
}

}  // namespace

int main() {
  using namespace echo::core;
  const std::unordered_map<std::string, std::string> params{
      {"b", "2"}, {"a", "1"}};

  Check(CalculateMd5("abc") == "900150983cd24fb0d6963f7d28e17f72",
        "MD5 known-answer");
  Check(SignatureWebParams(params) == "70ccbef64fdcc9271fe883d1d7f07395",
        "web signature known-answer");
  Check(SignatureRegisterParams({{"a", "3"}, {"b", "1"}, {"c", "2"}}) ==
            "a7836eb68ad8a7b16376d2f1b49bee53",
        "register signature known-answer");
  Check(SignatureAndroidParams(params, "payload", KuGouSaltKind::Lite) ==
            "3122a9bd747d6e8abaf99e1365461e42",
        "lite Android signature known-answer");
  Check(SignatureAndroidParams(params, "payload", KuGouSaltKind::Standard) ==
            "69ed10c9c9cf1b400e3852c399e549b8",
        "standard Android signature known-answer");
  Check(SignParamsKey("payload", "1014", "20000", KuGouSaltKind::Lite) ==
            "cdace4f050d9a84e76ad81c2c553046f",
        "lite params key known-answer");
  Check(SignParamsKey("payload", "1014", "20000", KuGouSaltKind::Standard) ==
            "e29cd31070a0c50c9ee17723b6a6b672",
        "standard params key known-answer");
  Check(SignKey("hash", "mid", "", "3116", KuGouSaltKind::Lite) ==
            "713b95bd2d636e864a27a2c1d15c2628",
        "lite song key known-answer");
  Check(SignKey("hash", "mid", "", "3116", KuGouSaltKind::Standard) ==
            "0e8aaf3b9512ed840b522cd8107ff944",
        "standard song key known-answer");

  Check(std::string(AndroidSalt(KuGouSaltKind::Lite)) ==
            "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" &&
        std::string(AndroidSalt(KuGouSaltKind::Standard)) ==
            "OIlwieks28dk2k092lksi2UIkp",
        "Android salt values");
  Check(std::string(KeySalt(KuGouSaltKind::Lite)) ==
            "185672dd44712f60bb1736df5a377e82" &&
        std::string(KeySalt(KuGouSaltKind::Standard)) ==
            "57ae12eb6890223e355ccfcb74edf70d",
        "song key salt values");

  Check(RsaRawEncrypt("abc") ==
            "376D685945E1F70C7FBAB3B70195788F335545730B31121866EEACC6BB556652"
            "5C9A878CCB7D36138848E12844410209685DF295C32232FFABD1FD0230EDAF8"
            "4619F7BC1EDA0CF1D4B9091A295D34395DE2497D4121825244ABA4EBF613EBE"
            "28B1C962A7ECA3B2E67CA99846AADE93776B5985A298257A9EF5B5589C31E9176A",
        "right-aligned RSA raw known-answer");
  Check(RsaRawEncryptRef("abc") ==
            "6476f0a0095a54fd2c14c2ff51edabe8ec33f8571760e510d78ec44eed905a2a"
            "221e455c83dab3e9973344559bb8edc657e1c4b5fa22814f394aedae2aa806311"
            "5b313a5373ea50e5d044fb4d551428c3f60d4d49f6ddd1fca1b2705c3b1b5cbc68"
            "43265838bd8bec54e97075661d5f6b722792949229485bca4dd806b6b56f5",
        "left-aligned RSA raw known-answer");
  Check(RsaRawEncryptRef("abc", KuGouSaltKind::Lite) ==
            "1411c3965fd9e73da53c5ead6e21bea112aa8a04e44843fd0f65d67bc712c352"
            "c75267e89e0a80b8897d673906d2a248d2ba7246056a865756722b633c54976a"
            "48e1d4fad63be86982fc0bef305d31ad0ab9562305ee504dcf8ffe16cfdbe7eb"
            "638f2c5b7eb6bc82611413161fc07f884e184cd6e1c9229445ab8291d99872ab",
        "lite left-aligned RSA raw known-answer");

  Check(AesCbcEncryptHex("native crypto vector", "0123456789abcdef",
                         "abcdef0123456789") ==
            "f8eef3be485d5eea6998f782943036a9de8a8cdd36a40152407c1f6eec6e322d",
        "AES-CBC hex encryption known-answer");
  Check(AesCbcDecryptHex(
            "f8eef3be485d5eea6998f782943036a9de8a8cdd36a40152407c1f6eec6e322d",
            "0123456789abcdef", "abcdef0123456789") == "native crypto vector",
        "AES-CBC hex decryption known-answer");
  Check(AesCbcEncryptBase64("native crypto vector", "0123456789abcdef",
                            "abcdef0123456789") ==
            "+O7zvkhdXuppmPeClDA2qd6KjN02pAFSQHwfbuxuMi0=",
        "AES-CBC Base64 encryption known-answer");
  Check(PlaylistAesDecrypt("BDPNqPe3ZSWUxvxCBQy6jF5GjLqsTaVIiV+HI3NfnWc=",
                           "abcdef") == "playlist-known-answer",
        "playlist AES decryption known-answer");
  Check(Base64EncodeBytes("Hello") == "SGVsbG8=",
        "raw byte Base64 known-answer");
  Check(HexStringToDecimalString("deadbeef") == "3735928559",
        "hex to decimal known-answer");
  Check(CalculateAndroidMid("abc") == "191415658344158766168031473277922803570",
        "Android mid known-answer");

  const auto encryptedStandard = RsaPkcs1Encrypt("abc");
  const auto encryptedLite = RsaPkcs1Encrypt("abc", KuGouSaltKind::Lite);
  const auto isUpperHexCipher = [](const std::string& value) {
    return value.size() == 256 && value.find_first_not_of("0123456789ABCDEF") ==
        std::string::npos;
  };
  Check(isUpperHexCipher(encryptedStandard) && isUpperHexCipher(encryptedLite),
        "randomized PKCS#1 RSA ciphertext shape (no fixed KAT)");

  const auto playlist = PlaylistAesEncrypt("round-trip-only");
  Check(playlist.key.size() == 6 &&
        playlist.key.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos &&
        PlaylistAesDecrypt(playlist.data, playlist.key) == "round-trip-only",
        "randomized playlist AES round-trip only");
  return failures == 0 ? 0 : 1;
}

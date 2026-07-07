#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/common/digest_legacy.h"
#include "ebbackup/common/digest_standard.h"

namespace ebbackup {
namespace crypto {
namespace {

std::vector<uint8_t> DecodeHex(const char* hex) {
  std::vector<uint8_t> out;
  const std::string s(hex);
  out.resize(s.size() / 2);
  EXPECT_TRUE(HexToBytes(s, out.data(), out.size()));
  return out;
}

TEST(DigestStandardTest, Sha256EmptyMatchesLegacy) {
  uint8_t std_out[32]{};
  uint8_t leg_out[32]{};
  Sha256Standard(nullptr, 0, std_out);
  Sha256Legacy(nullptr, 0, leg_out);
  EXPECT_EQ(std::memcmp(std_out, leg_out, 32), 0);
}

TEST(DigestStandardTest, Sha256Abc) {
  uint8_t out[32]{};
  Sha256Standard(reinterpret_cast<const uint8_t*>("abc"), 3, out);
  const auto expected = DecodeHex(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(std::memcmp(out, expected.data(), 32), 0);
}

TEST(DigestStandardTest, HmacSha256Case2) {
  uint8_t key[20];
  std::memset(key, 0x0b, sizeof(key));
  const uint8_t data[] = "Hi There";
  uint8_t out[32]{};
  HmacSha256Standard(key, sizeof(key), data, sizeof(data) - 1, out);
  uint8_t legacy[32]{};
  HmacSha256(DigestAlgo::kLegacy, key, sizeof(key), data, sizeof(data) - 1,
             legacy);
  EXPECT_EQ(std::memcmp(out, legacy, 32), 0);
}

TEST(DigestStandardTest, Pbkdf2PasswordSaltOneIteration) {
  const std::string password = "password";
  const std::string salt = "salt";
  uint8_t out[32]{};
  Pbkdf2Sha256Standard(reinterpret_cast<const uint8_t*>(password.data()),
                       password.size(),
                       reinterpret_cast<const uint8_t*>(salt.data()),
                       salt.size(), 1, out);
  const auto expected = DecodeHex(
      "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  EXPECT_EQ(std::memcmp(out, expected.data(), 32), 0);
}

TEST(DigestStandardTest, Pbkdf2Rfc7914PasswdSalt64) {
  const std::string password = "passwd";
  const std::string salt = "salt";
  uint8_t out[64]{};
  Pbkdf2Sha256StandardLen(reinterpret_cast<const uint8_t*>(password.data()),
                          password.size(),
                          reinterpret_cast<const uint8_t*>(salt.data()),
                          salt.size(), 1, out, 64);
  const auto expected = DecodeHex(
      "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
      "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
  EXPECT_EQ(std::memcmp(out, expected.data(), 64), 0);
}

TEST(DigestStandardTest, Pbkdf2Rfc7914PasswordNaCl80000) {
  const std::string password = "Password";
  const std::string salt = "NaCl";
  uint8_t out[64]{};
  Pbkdf2Sha256StandardLen(reinterpret_cast<const uint8_t*>(password.data()),
                          password.size(),
                          reinterpret_cast<const uint8_t*>(salt.data()),
                          salt.size(), 80000, out, 64);
  const auto expected = DecodeHex(
      "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1"
      "d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
  EXPECT_EQ(std::memcmp(out, expected.data(), 64), 0);
}

TEST(DigestStandardTest, ContentHashRouterStandard) {
  uint8_t out[32]{};
  ContentHash(DigestAlgo::kStandard,
                reinterpret_cast<const uint8_t*>("abc"), 3, out);
  const auto expected = DecodeHex(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(std::memcmp(out, expected.data(), 32), 0);
}

}  // namespace
}  // namespace crypto
}  // namespace ebbackup

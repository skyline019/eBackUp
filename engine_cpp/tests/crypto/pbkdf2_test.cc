#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ebbackup/common/digest.h"
#include "ebbackup/crypto/aes_gcm.h"

namespace ebbackup {
namespace crypto {
namespace {

TEST(Pbkdf2Test, Sha256AbcGolden) {
  uint8_t out[32]{};
  Sha256(reinterpret_cast<const uint8_t*>("abc"), 3, out);
  const uint8_t expected[32] = {
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
      0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
      0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  EXPECT_EQ(std::memcmp(out, expected, 32), 0);
}

TEST(Pbkdf2Test, HmacSha256Golden) {
  uint8_t key[20];
  std::memset(key, 0x0b, sizeof(key));
  const uint8_t data[] = "Hi There";
  uint8_t out[32]{};
  HmacSha256(key, sizeof(key), data, sizeof(data) - 1, out);
  const uint8_t expected[32] = {
      0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce,
      0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
      0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
  EXPECT_EQ(std::memcmp(out, expected, 32), 0);
}

TEST(Pbkdf2Test, OneIterationGolden) {
  const std::string password = "password";
  const std::string salt = "salt";
  uint8_t out[32]{};
  Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(password.data()),
               password.size(),
               reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), 1,
               out);
  const uint8_t expected[32] = {
      0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c, 0x43, 0xe7, 0x22, 0x52,
      0x56, 0xc4, 0xf8, 0x37, 0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48,
      0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b};
  EXPECT_EQ(std::memcmp(out, expected, 32), 0);
}

TEST(Pbkdf2Test, DeriveContentKeyUses100kIterations) {
  const std::string password = "test-pass";
  uint8_t salt[16]{};
  for (size_t i = 0; i < 16; ++i) salt[i] = static_cast<uint8_t>(i + 1);
  uint8_t via_derive[32]{};
  ASSERT_TRUE(DeriveContentKey(password, salt, via_derive).ok());
  uint8_t direct[32]{};
  Pbkdf2Sha256(reinterpret_cast<const uint8_t*>(password.data()),
               password.size(), salt, 16, 100000, direct);
  EXPECT_EQ(std::memcmp(via_derive, direct, 32), 0);
}

}  // namespace
}  // namespace crypto
}  // namespace ebbackup

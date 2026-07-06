#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/crypto/gcm_portable.h"

namespace ebbackup {
namespace crypto {
namespace {

TEST(AesGcmPortableTest, RoundTrip) {
  uint8_t key[32]{};
  for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);
  const std::vector<uint8_t> plain = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                                      30, 31, 32, 33, 34, 35};
  std::vector<uint8_t> encrypted;
  ASSERT_TRUE(Aes256GcmEncryptPortable(key, plain.data(), plain.size(), &encrypted).ok());
  EXPECT_EQ(encrypted.size(), kAesGcmNonceSize + plain.size() + kAesGcmTagSize);

  std::vector<uint8_t> decrypted;
  ASSERT_TRUE(Aes256GcmDecryptPortable(key, encrypted.data(), encrypted.size(), &decrypted)
                  .ok());
  EXPECT_EQ(decrypted, plain);
}

TEST(AesGcmPortableTest, TamperedTagFails) {
  uint8_t key[32]{};
  key[0] = 42;
  const std::vector<uint8_t> plain = {'h', 'e', 'l', 'l', 'o'};
  std::vector<uint8_t> encrypted;
  ASSERT_TRUE(Aes256GcmEncryptPortable(key, plain.data(), plain.size(), &encrypted).ok());
  encrypted.back() ^= 0x01;
  std::vector<uint8_t> decrypted;
  EXPECT_FALSE(
      Aes256GcmDecryptPortable(key, encrypted.data(), encrypted.size(), &decrypted).ok());
}

TEST(AesGcmPortableTest, PublicApiUsesPortablePath) {
  uint8_t key[32]{};
  for (size_t i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(0xA0 + i);
  const std::vector<uint8_t> plain(1024, 0x5A);
  std::vector<uint8_t> encrypted;
  ASSERT_TRUE(Aes256GcmEncrypt(key, plain.data(), plain.size(), &encrypted).ok());
  std::vector<uint8_t> decrypted;
  ASSERT_TRUE(Aes256GcmDecrypt(key, encrypted.data(), encrypted.size(), &decrypted).ok());
  EXPECT_EQ(decrypted, plain);
}

}  // namespace
}  // namespace crypto
}  // namespace ebbackup

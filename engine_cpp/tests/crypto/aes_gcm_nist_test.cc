#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/crypto/aes_gcm.h"
#include "ebbackup/crypto/gcm_portable.h"

namespace ebbackup {
namespace crypto {
namespace {

std::vector<uint8_t> HexToVec(const char* hex) {
  std::vector<uint8_t> out;
  const std::string s(hex);
  out.resize(s.size() / 2);
  EXPECT_TRUE(HexToBytes(s, out.data(), out.size()));
  return out;
}

std::vector<uint8_t> BuildWireBlob(const uint8_t nonce[12],
                                   const std::vector<uint8_t>& cipher,
                                   const std::vector<uint8_t>& tag) {
  std::vector<uint8_t> blob;
  blob.insert(blob.end(), nonce, nonce + kAesGcmNonceSize);
  blob.insert(blob.end(), cipher.begin(), cipher.end());
  blob.insert(blob.end(), tag.begin(), tag.end());
  return blob;
}

TEST(AesGcmNistTest, Case13EmptyPlaintextPortable) {
  const auto key = HexToVec(
      "0000000000000000000000000000000000000000000000000000000000000000");
  const auto iv = HexToVec("000000000000000000000000");
  const auto tag = HexToVec("530f8afbc74536b9a963b4f1c4cb738b");
  ASSERT_EQ(key.size(), 32u);
  ASSERT_EQ(iv.size(), 12u);
  ASSERT_EQ(tag.size(), 16u);

  const std::vector<uint8_t> blob = BuildWireBlob(iv.data(), {}, tag);
  std::vector<uint8_t> plain;
  ASSERT_TRUE(
      Aes256GcmDecryptPortableEx(key.data(), blob.data(), blob.size(), nullptr,
                                 0, &plain)
          .ok());
  EXPECT_TRUE(plain.empty());

  std::vector<uint8_t> sealed;
  ASSERT_TRUE(Aes256GcmEncryptPortableEx(key.data(), iv.data(), nullptr, 0,
                                         nullptr, 0, &sealed)
                  .ok());
  EXPECT_EQ(std::vector<uint8_t>(sealed.end() - kAesGcmTagSize, sealed.end()),
            tag);
}

TEST(AesGcmNistTest, Case14NonEmptyPlaintextPortable) {
  const auto key = HexToVec(
      "0000000000000000000000000000000000000000000000000000000000000000");
  const auto iv = HexToVec("000000000000000000000000");
  const auto pt = HexToVec("00000000000000000000000000000000");
  const auto ct = HexToVec("cea7403d4d606b6e074ec5d3baf39d18");
  const auto tag = HexToVec("d0d1c8a799996bf0265b98b5d48ab919");

  const std::vector<uint8_t> blob = BuildWireBlob(iv.data(), ct, tag);
  std::vector<uint8_t> plain;
  ASSERT_TRUE(
      Aes256GcmDecryptPortableEx(key.data(), blob.data(), blob.size(), nullptr,
                                 0, &plain)
          .ok());
  EXPECT_EQ(plain, pt);

  std::vector<uint8_t> sealed;
  ASSERT_TRUE(Aes256GcmEncryptPortableEx(key.data(), iv.data(), nullptr, 0,
                                         pt.data(), pt.size(), &sealed)
                  .ok());
  EXPECT_EQ(std::vector<uint8_t>(sealed.begin() + kAesGcmNonceSize,
                               sealed.end() - kAesGcmTagSize),
            ct);
  EXPECT_EQ(std::vector<uint8_t>(sealed.end() - kAesGcmTagSize, sealed.end()),
            tag);
}

TEST(AesGcmNistTest, PublicApiDecryptsNistBlob) {
  const auto key = HexToVec(
      "0000000000000000000000000000000000000000000000000000000000000000");
  const auto iv = HexToVec("000000000000000000000000");
  const auto tag = HexToVec("530f8afbc74536b9a963b4f1c4cb738b");
  const std::vector<uint8_t> blob = BuildWireBlob(iv.data(), {}, tag);
  std::vector<uint8_t> plain;
  ASSERT_TRUE(Aes256GcmDecrypt(key.data(), blob.data(), blob.size(), &plain).ok());
  EXPECT_TRUE(plain.empty());
}

}  // namespace
}  // namespace crypto
}  // namespace ebbackup

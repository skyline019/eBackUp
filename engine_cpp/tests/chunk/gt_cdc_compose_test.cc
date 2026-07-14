#include <gtest/gtest.h>

#include "ebbackup/chunk/gt_cdc.h"
#include "ebbackup/chunk/gt_cdc_internal.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(GtCdcComposeTest, AlphaPowTable) {
  uint32_t pow[gtcdc_internal::kAlphaPowTableSize]{};
  gtcdc_internal::InitAlphaPowTable(0x00010001u, pow);
  EXPECT_EQ(pow[0], 1u);
  EXPECT_EQ(pow[1], 0x00010001u);
  EXPECT_EQ(pow[2], 0x00010001u * 0x00010001u);
}

TEST(GtCdcComposeTest, BlockFingerprintMatchesFeedFromZero) {
  const std::string data = test::MakeSyntheticData(4096, 19);
  uint32_t beta[256]{};
  gtcdc_internal::InitBetaTable(beta);
  constexpr uint32_t alpha = 0x00010001u;
  const uint32_t fp = gtcdc_internal::BlockFingerprint(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), alpha, beta);
  const uint32_t feed =
      gtcdc_internal::RabinFeedRange(0, reinterpret_cast<const uint8_t*>(data.data()),
                                     data.size(), alpha, beta);
  EXPECT_EQ(fp, feed);
}

TEST(GtCdcComposeTest, ComposeBlockFastMatchesSequential) {
  const std::string data = test::MakeSyntheticData(8192, 23);
  uint32_t beta[256]{};
  gtcdc_internal::InitBetaTable(beta);
  uint32_t pow[gtcdc_internal::kAlphaPowTableSize]{};
  constexpr uint32_t alpha = 0x00010001u;
  gtcdc_internal::InitAlphaPowTable(alpha, pow);

  for (uint32_t h0 : {0u, 1u, 0xDEADBEEFu, 0x12345678u}) {
    for (size_t len : {1u, 7u, 8u, 63u, 64u}) {
      if (len > data.size()) continue;
      const uint32_t fp = gtcdc_internal::BlockFingerprint(
          reinterpret_cast<const uint8_t*>(data.data()), len, alpha, beta);
      const uint32_t fast =
          gtcdc_internal::ComposeBlockFast(h0, fp, pow[len]);
      const uint32_t seq = gtcdc_internal::ComposeBlock(
          h0, reinterpret_cast<const uint8_t*>(data.data()), len, alpha, beta);
      EXPECT_EQ(fast, seq) << "h0=" << h0 << " len=" << len;
    }
  }
}

TEST(GtCdcComposeTest, BlockFingerprint64MatchesBlockFingerprint) {
  const std::string data = test::MakeSyntheticData(256, 41);
  uint32_t beta[256]{};
  gtcdc_internal::InitBetaTable(beta);
  constexpr uint32_t alpha = 0x00010001u;
  for (size_t off = 0; off + 64 <= data.size(); off += 17) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data()) + off;
    const uint32_t fp64 = gtcdc_internal::BlockFingerprint64(ptr, alpha, beta);
    const uint32_t fp =
        gtcdc_internal::BlockFingerprint(ptr, 64, alpha, beta);
    EXPECT_EQ(fp64, fp) << "off=" << off;
  }
}

TEST(GtCdcComposeTest, ComposeBlockFastLinearity) {
  uint32_t beta[256]{};
  gtcdc_internal::InitBetaTable(beta);
  uint32_t pow[gtcdc_internal::kAlphaPowTableSize]{};
  constexpr uint32_t alpha = 0x00010001u;
  gtcdc_internal::InitAlphaPowTable(alpha, pow);

  const uint8_t block_a[] = {1, 2, 3, 4, 5, 6, 7, 8};
  const uint8_t block_b[] = {9, 10, 11, 12, 13, 14, 15, 16};
  const uint8_t combined[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

  const uint32_t fp_a = gtcdc_internal::BlockFingerprint(block_a, 8, alpha, beta);
  const uint32_t fp_b = gtcdc_internal::BlockFingerprint(block_b, 8, alpha, beta);
  const uint32_t fp_ab = gtcdc_internal::BlockFingerprint(combined, 16, alpha, beta);

  const uint32_t h_mid = gtcdc_internal::ComposeBlockFast(0, fp_a, pow[8]);
  const uint32_t h_fast = gtcdc_internal::ComposeBlockFast(h_mid, fp_b, pow[8]);
  const uint32_t h_seq =
      gtcdc_internal::ComposeBlock(0, combined, 16, alpha, beta);
  EXPECT_EQ(h_fast, h_seq);
  EXPECT_EQ(h_fast, gtcdc_internal::ComposeBlockFast(0, fp_ab, pow[16]));
}

TEST(GtCdcComposeTest, FeedRangeComposeMatchesRabinFeedRange) {
  const std::string data = test::MakeSyntheticData(128 * 1024, 29);
  uint32_t beta[256]{};
  gtcdc_internal::InitBetaTable(beta);
  uint32_t pow[gtcdc_internal::kAlphaPowTableSize]{};
  constexpr uint32_t alpha = 0x00010001u;
  gtcdc_internal::InitAlphaPowTable(alpha, pow);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t len : {64u, 1024u, 65536u}) {
    const uint32_t feed = gtcdc_internal::RabinFeedRange(0, bytes, len, alpha, beta);
    const uint32_t compose =
        gtcdc_internal::FeedRangeCompose(bytes, len, 0, alpha, beta, 64, pow);
    EXPECT_EQ(feed, compose) << "len=" << len;
  }
}

}  // namespace
}  // namespace ebbackup

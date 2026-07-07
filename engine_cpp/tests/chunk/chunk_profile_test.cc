#include <gtest/gtest.h>

#include "ebbackup/chunk/chunk_profile.h"
#include "ebbackup/chunk/fast_cdc.h"

namespace ebbackup {
namespace {

TEST(ChunkProfileTest, DefaultProfileMatchesLegacyBounds) {
  const FastCdcConfig cfg = FastCdcConfigForProfile(ChunkProfileMode::kDefault);
  EXPECT_EQ(cfg.min_size, 64u * 1024u);
  EXPECT_EQ(cfg.avg_size, 256u * 1024u);
  EXPECT_EQ(cfg.max_size, 1024u * 1024u);
}

TEST(ChunkProfileTest, AutoSelectsByFileSize) {
  const EbHcrboConfig small =
      EbHcrboConfigForFileSize(128 * 1024, ChunkProfileMode::kAuto,
                               DigestAlgo::kLegacy);
  EXPECT_EQ(small.fast.min_size, 4u * 1024u);

  const EbHcrboConfig large =
      EbHcrboConfigForFileSize(80u * 1024u * 1024u, ChunkProfileMode::kAuto,
                               DigestAlgo::kLegacy);
  EXPECT_EQ(large.fast.max_size, 4u * 1024u * 1024u);

  const EbHcrboConfig mid =
      EbHcrboConfigForFileSize(512 * 1024, ChunkProfileMode::kAuto,
                               DigestAlgo::kLegacy);
  EXPECT_EQ(mid.fast.avg_size, 256u * 1024u);
}

}  // namespace
}  // namespace ebbackup

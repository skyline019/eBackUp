#include <gtest/gtest.h>

#include "ebbackup/chunk/fast_cdc.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(FastCdcTest, ChunkBoundsRespectMinMax) {
  FastCdcConfig cfg;
  cfg.min_size = 64 * 1024;
  cfg.max_size = 1024 * 1024;
  cfg.avg_size = 256 * 1024;
  FastCdcSlice chunker(cfg);
  const std::string data = test::MakeSyntheticData(5 * 1024 * 1024, 11);
  std::vector<ChunkDescriptor> chunks;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &chunks)
                  .ok());
  ASSERT_GT(chunks.size(), 1u);
  for (size_t i = 0; i + 1 < chunks.size(); ++i) {
    EXPECT_GE(chunks[i].length, cfg.min_size);
    EXPECT_LE(chunks[i].length, cfg.max_size);
  }
}

}  // namespace
}  // namespace ebbackup

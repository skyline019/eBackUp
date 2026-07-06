#include <gtest/gtest.h>

#include "ebbackup/chunk/fast_cdc.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(FastCdcTest, DeterministicBoundaries) {
  const std::string data = test::MakeSyntheticData(3 * 1024 * 1024, 42);
  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> a;
  std::vector<ChunkDescriptor> b;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &a)
                  .ok());
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &b)
                  .ok());
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i], b[i]) << "chunk index " << i;
  }
}

TEST(FastCdcTest, SmallFileSingleChunk) {
  const std::string data = test::MakeSyntheticData(1024, 7);
  FastCdcSlice chunker;
  std::vector<ChunkDescriptor> chunks;
  ASSERT_TRUE(chunker.Chunk(reinterpret_cast<const uint8_t*>(data.data()),
                            data.size(), &chunks)
                  .ok());
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].offset, 0u);
  EXPECT_EQ(chunks[0].length, data.size());
}

}  // namespace
}  // namespace ebbackup

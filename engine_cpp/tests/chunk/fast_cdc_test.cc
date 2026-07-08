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

TEST(FastCdcTest, ChunkCutsUntilMatchesFullCuts) {
  constexpr size_t kFileSize = 256 * 1024 * 1024;
  const std::string data = test::MakeSyntheticData(kFileSize, 55);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
  FastCdcSlice chunker;

  std::vector<size_t> full_offsets;
  std::vector<uint32_t> full_lengths;
  ASSERT_TRUE(chunker.ChunkCuts(bytes, data.size(), &full_offsets, &full_lengths)
                  .ok());

  std::vector<size_t> window_offsets;
  std::vector<uint32_t> window_lengths;
  std::vector<size_t> merged_offsets;
  std::vector<uint32_t> merged_lengths;
  FastCdcCutCursor cursor{};
  constexpr size_t kFeed = 32u * 1024u * 1024u;
  for (size_t feed_off = 0; feed_off < data.size(); feed_off += kFeed) {
    const size_t feed_end = std::min(feed_off + kFeed, data.size());
    bool complete = false;
    ASSERT_TRUE(chunker
                    .ChunkCutsUntil(bytes, data.size(), feed_end, &cursor,
                                    &window_offsets, &window_lengths, &complete)
                    .ok());
    merged_offsets.insert(merged_offsets.end(), window_offsets.begin(),
                          window_offsets.end());
    merged_lengths.insert(merged_lengths.end(), window_lengths.begin(),
                          window_lengths.end());
    if (complete) break;
  }

  ASSERT_EQ(merged_offsets.size(), full_offsets.size());
  ASSERT_EQ(merged_lengths.size(), full_lengths.size());
  for (size_t i = 0; i < full_offsets.size(); ++i) {
    EXPECT_EQ(merged_offsets[i], full_offsets[i]) << "offset index " << i;
    EXPECT_EQ(merged_lengths[i], full_lengths[i]) << "length index " << i;
  }
}

}  // namespace
}  // namespace ebbackup

#include <gtest/gtest.h>

#include <vector>

#include "ebbackup/store/chunk_index.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ChunkIndexTest, RebuildFromScanMatchesAppend) {
  const std::string dir = test::TempDir("chunk_index");
  const std::string chunks = dir + "/chunks";
  ChunkStore store(chunks);
  ASSERT_TRUE(store.Open().ok());

  const uint8_t data1[] = "alpha-chunk";
  const uint8_t data2[] = "beta-chunk-2";
  uint8_t hash1[32];
  uint8_t hash2[32];
  ASSERT_TRUE(store.Put(data1, sizeof(data1) - 1, hash1).ok());
  ASSERT_TRUE(store.Put(data2, sizeof(data2) - 1, hash2).ok());

  std::vector<ChunkIndexEntry> rebuilt;
  uint64_t size = 0;
  ASSERT_TRUE(ChunkIndexFile::RebuildFromScan(chunks, &rebuilt, &size).ok());
  EXPECT_EQ(rebuilt.size(), 2u);
  EXPECT_EQ(size, store.file_size());
}

}  // namespace
}  // namespace ebbackup

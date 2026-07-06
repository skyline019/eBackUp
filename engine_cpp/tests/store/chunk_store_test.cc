#include <gtest/gtest.h>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ChunkStoreTest, DedupSkipWrite) {
  const std::string dir = test::TempDir("chunk_store_dedup");
  ChunkStore store(dir + "/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = test::MakeSyntheticData(4096, 3);
  uint8_t hash1[32];
  uint8_t hash2[32];
  bool wrote1 = false;
  bool wrote2 = false;
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash1, &wrote1)
                  .ok());
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash2, &wrote2)
                  .ok());
  EXPECT_TRUE(wrote1);
  EXPECT_FALSE(wrote2);
  EXPECT_EQ(std::memcmp(hash1, hash2, 32), 0);
  EXPECT_EQ(store.record_count(), 1u);
}

TEST(ChunkStoreTest, CorruptRefusesRead) {
  const std::string dir = test::TempDir("chunk_store_corrupt");
  ChunkStore store(dir + "/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = "corrupt-me";
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());
  ASSERT_TRUE(store.CorruptRecordAtOffsetForTest(0).ok());
  std::vector<uint8_t> out;
  EXPECT_EQ(store.Get(hash, &out).code(), StatusCode::kCorrupt);
}

TEST(ChunkStoreTest, CorruptIndexFailsOpen) {
  const std::string dir = test::TempDir("chunk_store_bad_index");
  ChunkStore store(dir + "/chunks");
  const std::string payload = "bad-index";
  uint8_t hash[32];
  ASSERT_TRUE(store.Open().ok());
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());
  ASSERT_TRUE(store.CorruptRecordAtOffsetForTest(0).ok());
  ChunkStore store2(dir + "/chunks");
  EXPECT_EQ(store2.Open().code(), StatusCode::kCorrupt);
}

}  // namespace
}  // namespace ebbackup

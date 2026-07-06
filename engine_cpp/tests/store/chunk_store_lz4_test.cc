#include <gtest/gtest.h>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/chunk_store.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(ChunkStoreLz4Test, PutGetRoundTrip) {
  const std::string dir = test::TempDir("chunk_store_lz4");
  ChunkStore store(dir + "/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = test::MakeSyntheticData(8192, 11);
  uint8_t hash[32];
  ChunkStorePutOptions opts{};
  opts.use_lz4 = true;
  bool wrote = false;
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash, &wrote, &opts)
                  .ok());
  EXPECT_TRUE(wrote);
  std::vector<uint8_t> out;
  ASSERT_TRUE(store.Get(hash, &out).ok());
  EXPECT_EQ(out.size(), payload.size());
  EXPECT_EQ(out, std::vector<uint8_t>(payload.begin(), payload.end()));
}

TEST(ChunkStoreLz4Test, DedupUsesContentHash) {
  const std::string dir = test::TempDir("chunk_store_lz4_dedup");
  ChunkStore store(dir + "/chunks");
  ASSERT_TRUE(store.Open().ok());
  const std::string payload = test::MakeSyntheticData(4096, 5);
  uint8_t hash1[32];
  uint8_t hash2[32];
  ChunkStorePutOptions opts{};
  opts.use_lz4 = true;
  bool wrote1 = false;
  bool wrote2 = false;
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash1, &wrote1, &opts)
                  .ok());
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash2, &wrote2, &opts)
                  .ok());
  EXPECT_TRUE(wrote1);
  EXPECT_FALSE(wrote2);
  EXPECT_EQ(store.record_count(), 1u);
}

}  // namespace
}  // namespace ebbackup

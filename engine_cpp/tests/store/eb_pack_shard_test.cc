#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/eb_pack.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(EbPackShardTest, DistributesAcrossShards) {
  const std::string dir = test::TempDir("ebpack_shard");
  EbPackShardSet shards(dir, 99, 4u * 1024u * 1024u, 32);

  std::set<uint32_t> shard_ids;
  for (uint8_t seed = 0; seed < 32; ++seed) {
    const std::string payload = test::MakeSyntheticData(32 * 1024, seed);
    uint8_t hash[32];
    ContentHash(DigestAlgo::kStandard,
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                hash);
    EbPackRecordRef ref{};
    ASSERT_TRUE(shards
                    .AppendRecord(hash, reinterpret_cast<const uint8_t*>(payload.data()),
                                  payload.size(),
                                  static_cast<uint32_t>(payload.size()),
                                  ChunkCodec::kRaw, &ref)
                    .ok());
    EXPECT_NE(ref.pack_path.find("-s"), std::string::npos);
    shard_ids.insert(static_cast<uint32_t>(EbPackShardSet::ShardForHash(hash)));
  }
  EXPECT_GT(shard_ids.size(), 4u);
  ASSERT_TRUE(shards.FlushAllOpenPacks(true).ok());
  ASSERT_TRUE(shards.FsyncAll().ok());
}

TEST(EbPackShardTest, ChunkStoreRoundTrip) {
  const std::string repo = test::TempDir("ebpack_shard_store");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  ChunkStore store(repo + "/data/chunks");
  store.SetUseEbPack(true);
  store.SetTxnId(7);
  ASSERT_TRUE(store.Open().ok());
  ASSERT_TRUE(store.BeginAppendSession().ok());

  for (uint8_t seed = 0; seed < 16; ++seed) {
    const std::string payload = test::MakeSyntheticData(64 * 1024, seed + 3);
    uint8_t hash[32];
    ASSERT_TRUE(store
                    .Put(reinterpret_cast<const uint8_t*>(payload.data()),
                         payload.size(), hash)
                    .ok());
  }

  ASSERT_TRUE(store.Flush().ok());
  for (uint8_t seed = 0; seed < 16; ++seed) {
    const std::string payload = test::MakeSyntheticData(64 * 1024, seed + 3);
    uint8_t hash[32];
    ContentHash(DigestAlgo::kLegacy,
                reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                hash);
    std::vector<uint8_t> out;
    ASSERT_TRUE(store.Get(hash, &out).ok());
    EXPECT_EQ(out.size(), payload.size());
  }

  ASSERT_TRUE(store.EndAppendSession().ok());
}

}  // namespace
}  // namespace ebbackup

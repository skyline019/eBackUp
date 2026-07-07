#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ebbackup/common/digest.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/eb_pack.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(EbPackTest, WriteReadRoundTrip) {
  const std::string dir = test::TempDir("ebpack_write");
  EbPackWriter writer(dir, 42);
  const std::string payload = test::MakeSyntheticData(128 * 1024, 9);
  uint8_t hash[32];
  ContentHash(DigestAlgo::kStandard,
              reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
              hash);

  EbPackRecordRef ref{};
  ASSERT_TRUE(writer
                  .AppendRecord(hash, reinterpret_cast<const uint8_t*>(payload.data()),
                                payload.size(), static_cast<uint32_t>(payload.size()),
                                ChunkCodec::kRaw, &ref)
                  .ok());
  ASSERT_TRUE(writer.FlushOpenPack(true).ok());

  ChunkStore::ParsedHeader parsed{};
  std::vector<uint8_t> out;
  ASSERT_TRUE(ChunkStore::ReadEbPackRecordAt(ref.pack_path, ref.offset, &parsed, &out)
                  .ok());
  EXPECT_EQ(out.size(), payload.size());
  uint8_t verify[32];
  ContentHash(DigestAlgo::kStandard, out.data(), out.size(), verify);
  EXPECT_EQ(std::memcmp(verify, hash, 32), 0);
}

TEST(EbPackTest, ChunkStoreEbPackPutGet) {
  const std::string repo = test::TempDir("ebpack_store");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  ChunkStore store(repo + "/data/chunks");
  store.SetUseEbPack(true);
  store.SetTxnId(1);
  ASSERT_TRUE(store.Open().ok());
  ASSERT_TRUE(store.BeginAppendSession().ok());

  const std::string payload = test::MakeSyntheticData(256 * 1024, 11);
  uint8_t hash[32];
  ASSERT_TRUE(store
                  .Put(reinterpret_cast<const uint8_t*>(payload.data()),
                       payload.size(), hash)
                  .ok());
  ASSERT_TRUE(store.Flush().ok());
  ASSERT_TRUE(store.EndAppendSession().ok());

  std::vector<uint8_t> out;
  ASSERT_TRUE(store.Get(hash, &out).ok());
  EXPECT_EQ(out.size(), payload.size());
}

}  // namespace
}  // namespace ebbackup

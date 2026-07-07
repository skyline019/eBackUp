#include <gtest/gtest.h>

#include "ebbackup/engine/backup_engine.h"
#include "ebbackup/store/chunk_compactor.h"
#include "ebbackup/store/chunk_store.h"
#include "ebbackup/store/repo_stats.h"
#include "test_util.h"

namespace ebbackup {
namespace {

TEST(EbPackCompactTest, CompactShrinksAmplAndVerifyOk) {
  const std::string repo = test::TempDir("ebpack_compact");
  const std::string source = test::TempDir("ebpack_compact_src");
  const std::string dest = test::TempDir("ebpack_compact_dest");
  ASSERT_TRUE(test::InitV05Repo(repo).ok());
  test::WriteFile(source + "/data.bin", test::MakeSyntheticData(512 * 1024, 3));

  BackupEngine engine(repo);
  ASSERT_TRUE(engine.Open().ok());
  ASSERT_TRUE(engine.RunBackup(source).ok());

  ChunkStore store(repo + "/data/chunks");
  store.SetUseEbPack(true);
  store.SetUsePersistentIndex(true);
  store.SetTxnId(2);
  ASSERT_TRUE(store.Open().ok());
  ASSERT_TRUE(store.BeginAppendSession().ok());
  const std::string payload = test::MakeSyntheticData(32 * 1024, 99);
  uint8_t hash[32];
  ASSERT_TRUE(store.Put(reinterpret_cast<const uint8_t*>(payload.data()),
                      payload.size(), hash)
                  .ok());
  ASSERT_TRUE(store.Flush().ok());
  ASSERT_TRUE(store.EndAppendSession().ok());

  RepoStats before{};
  ASSERT_TRUE(ComputeRepoStats(repo, &before).ok());
  EXPECT_GT(before.ampl_ratio, 1.0);

  CompactReport report{};
  ASSERT_TRUE(CompactChunkStore(repo, false, &report).ok());
  EXPECT_LT(report.physical_after, report.physical_before);
  EXPECT_LE(report.ampl_ratio_after, 1.05);

  RepoStats after{};
  ASSERT_TRUE(ComputeRepoStats(repo, &after).ok());
  EXPECT_LE(after.ampl_ratio, 1.05);

  BackupEngine verify_engine(repo);
  ASSERT_TRUE(verify_engine.Open().ok());
  ASSERT_TRUE(verify_engine.Verify().ok());
  ASSERT_TRUE(verify_engine.Restore(dest).ok());
  EXPECT_TRUE(std::filesystem::exists(dest + "/data.bin"));
}

}  // namespace
}  // namespace ebbackup
